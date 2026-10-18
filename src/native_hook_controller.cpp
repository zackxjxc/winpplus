#include "native_hook_controller.h"
#include "native_route_state.h"

#include <tlhelp32.h>
#include <algorithm>
#include <utility>
#include <vector>

namespace {
using winpplus::NativeRouteState;

ULONGLONG CreationTime(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
    ULARGE_INTEGER time{};
    time.LowPart = created.dwLowDateTime;
    time.HighPart = created.dwHighDateTime;
    return time.QuadPart;
}

HANDLE ModuleSnapshot(DWORD pid) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) return snapshot;
    }
    return INVALID_HANDLE_VALUE;
}

HMODULE FindModule(DWORD pid, const std::wstring& path, bool fullPath, DWORD& error) {
    HANDLE snapshot = ModuleSnapshot(pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return nullptr;
    }
    MODULEENTRY32W entry{sizeof(entry)};
    HMODULE found{};
    if (Module32FirstW(snapshot, &entry)) {
        do {
            const wchar_t* candidate = fullPath ? entry.szExePath : entry.szModule;
            if (_wcsicmp(candidate, path.c_str()) == 0) {
                found = entry.hModule;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    error = ERROR_SUCCESS;
    CloseHandle(snapshot);
    return found;
}

// LoadLibraryW can be forwarded into KernelBase; resolve that module's address
// in the target process instead of assuming that our ASLR address is shared.
LPTHREAD_START_ROUTINE RemoteLoadLibrary(DWORD pid, DWORD& error) {
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    FARPROC load = kernel ? GetProcAddress(kernel, "LoadLibraryW") : nullptr;
    HMODULE owner{};
    if (!load || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(load), &owner)) {
        error = GetLastError();
        return nullptr;
    }
    wchar_t filename[MAX_PATH]{};
    if (!GetModuleFileNameW(owner, filename, ARRAYSIZE(filename))) {
        error = GetLastError();
        return nullptr;
    }
    const std::wstring localPath(filename);
    const auto slash = localPath.find_last_of(L"\\/");
    const auto moduleName = localPath.substr(slash == std::wstring::npos ? 0 : slash + 1);
    HMODULE remoteOwner = FindModule(pid, moduleName, false, error);
    if (!remoteOwner) {
        if (error == ERROR_SUCCESS) error = ERROR_MOD_NOT_FOUND;
        return nullptr;
    }
    const auto offset = reinterpret_cast<ULONG_PTR>(load) - reinterpret_cast<ULONG_PTR>(owner);
    return reinterpret_cast<LPTHREAD_START_ROUTINE>(reinterpret_cast<ULONG_PTR>(remoteOwner) + offset);
}

void PublishDisabled(NativeRouteState* state) {
    if (!state) return;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&state->enabled), 0);
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&state->validUntil), 0);
}
} // namespace

struct NativeHookController::Impl {
    struct Target {
        DWORD pid{};
        ULONGLONG created{};
        HANDLE process{};
        HANDLE thread{};
        void* remotePath{};
        ULONGLONG started{};
        ULONGLONG retryAfter{};
        DWORD error{};
        bool loaded{};
        HMODULE module{};
        bool hookReady{};
    };

    explicit Impl(std::wstring path) : dllPath(std::move(path)) {}
    std::wstring dllPath;
    HANDLE mapping{};
    NativeRouteState* state{};
    std::vector<Target> targets;
    bool enabled{};
    DWORD error{};
    ULONGLONG nextScan{};
    ULONG_PTR hookStatusOffset{};

    bool OpenState() {
        if (state) return true;
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
            sizeof(NativeRouteState), winpplus::kRouteStateMapping);
        if (!mapping) {
            error = GetLastError();
            return false;
        }
        state = static_cast<NativeRouteState*>(MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(NativeRouteState)));
        if (!state) {
            error = GetLastError();
            CloseHandle(mapping);
            mapping = nullptr;
            return false;
        }
        PublishDisabled(state);
        return true;
    }

    static void ReleaseTarget(Target& target, bool completed) {
        // A timed-out LoadLibrary thread may still read its argument. Do not free
        // that memory until completion (or until its process has exited).
        if (target.remotePath && completed) VirtualFreeEx(target.process, target.remotePath, 0, MEM_RELEASE);
        if (target.thread) CloseHandle(target.thread);
        if (target.process) CloseHandle(target.process);
        target.remotePath = nullptr;
        target.thread = nullptr;
        target.process = nullptr;
    }

    void Poll() {
        for (auto it = targets.begin(); it != targets.end();) {
            auto& target = *it;
            if (target.process && WaitForSingleObject(target.process, 0) == WAIT_OBJECT_0) {
                ReleaseTarget(target, true);
                it = targets.erase(it);
                nextScan = 0;
                continue;
            }
            if (target.thread) {
                const DWORD wait = WaitForSingleObject(target.thread, 0);
                if (wait == WAIT_OBJECT_0) {
                    DWORD exitCode{};
                    const bool exitRead = GetExitCodeThread(target.thread, &exitCode) != FALSE;
                    const DWORD exitError = exitRead ? ERROR_SUCCESS : GetLastError();
                    DWORD moduleError{};
                    target.module = FindModule(target.pid, dllPath, true, moduleError);
                    target.loaded = target.module != nullptr;
                    // A remote thread's exit code is only 32 bits, not a valid
                    // x64 module handle. The module list is authoritative.
                    target.error = target.loaded ? ERROR_SUCCESS :
                        (moduleError ? moduleError : (exitError ? exitError : ERROR_DLL_INIT_FAILED));
                    if (!target.loaded && exitRead && exitCode == 0) target.error = ERROR_DLL_INIT_FAILED;
                    if (!target.loaded) target.retryAfter = GetTickCount64() + 5000;
                    if (target.remotePath) VirtualFreeEx(target.process, target.remotePath, 0, MEM_RELEASE);
                    target.remotePath = nullptr;
                    CloseHandle(target.thread);
                    target.thread = nullptr;
                } else if (wait == WAIT_FAILED) {
                    target.error = GetLastError();
                } else if (GetTickCount64() - target.started >= 5000) {
                    target.error = WAIT_TIMEOUT;
                }
            }
            if (target.loaded && hookStatusOffset) {
                DWORD hookStatus{};
                SIZE_T read{};
                const auto address = reinterpret_cast<LPCVOID>(reinterpret_cast<ULONG_PTR>(target.module) + hookStatusOffset);
                if (!ReadProcessMemory(target.process, address, &hookStatus, sizeof(hookStatus), &read) || read != sizeof(hookStatus)) {
                    target.hookReady = false;
                    target.error = GetLastError();
                    if (!target.error) target.error = ERROR_READ_FAULT;
                } else if (hookStatus == 1) {
                    target.hookReady = true;
                    target.error = ERROR_SUCCESS;
                } else if (hookStatus & 0x80000000u) {
                    target.hookReady = false;
                    target.error = ERROR_INVALID_HOOK_HANDLE;
                } else {
                    target.hookReady = false;
                    target.error = ERROR_SUCCESS;
                }
            }
            ++it;
        }
    }

    void BeginInjection(Target& target) {
        target.error = ERROR_SUCCESS;
        target.retryAfter = GetTickCount64() + 5000;
        DWORD moduleError{};
        target.module = FindModule(target.pid, dllPath, true, moduleError);
        if (target.module) {
            target.loaded = true;
            return;
        }
        if (moduleError) {
            target.error = moduleError;
            return;
        }
        const auto load = RemoteLoadLibrary(target.pid, target.error);
        if (!load) return;
        const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
        target.remotePath = VirtualAllocEx(target.process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!target.remotePath) {
            target.error = GetLastError();
            return;
        }
        SIZE_T written{};
        if (!WriteProcessMemory(target.process, target.remotePath, dllPath.c_str(), bytes, &written) || written != bytes) {
            target.error = GetLastError();
            if (!target.error) target.error = ERROR_WRITE_FAULT;
            VirtualFreeEx(target.process, target.remotePath, 0, MEM_RELEASE);
            target.remotePath = nullptr;
            return;
        }
        target.thread = CreateRemoteThread(target.process, nullptr, 0, load, target.remotePath, 0, nullptr);
        if (!target.thread) {
            target.error = GetLastError();
            VirtualFreeEx(target.process, target.remotePath, 0, MEM_RELEASE);
            target.remotePath = nullptr;
            return;
        }
        target.started = GetTickCount64();
    }

    void Scan() {
        if (!hookStatusOffset) {
            HMODULE localImage = LoadLibraryExW(dllPath.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
            if (!localImage) {
                error = GetLastError();
                return;
            }
            FARPROC status = GetProcAddress(localImage, "WinPPlusHookStatus");
            if (status) hookStatusOffset = reinterpret_cast<ULONG_PTR>(status) - reinterpret_cast<ULONG_PTR>(localImage);
            else error = GetLastError();
            FreeLibrary(localImage);
            if (!hookStatusOffset) return;
        }
        DWORD session{};
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
            error = GetLastError();
            return;
        }
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            error = GetLastError();
            return;
        }
        PROCESSENTRY32W entry{sizeof(entry)};
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, L"ShellHost.exe") != 0) continue;
                DWORD targetSession{};
                if (!ProcessIdToSessionId(entry.th32ProcessID, &targetSession) || targetSession != session) continue;
                HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
                    PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD | SYNCHRONIZE, FALSE, entry.th32ProcessID);
                if (!process) {
                    error = GetLastError();
                    continue;
                }
                const ULONGLONG created = CreationTime(process);
                if (!created) {
                    error = GetLastError();
                    CloseHandle(process);
                    continue;
                }
                const auto found = std::find_if(targets.begin(), targets.end(), [&](const Target& target) {
                    return target.pid == entry.th32ProcessID && target.created == created;
                });
                if (found != targets.end()) {
                    CloseHandle(process);
                    // A failed attempt remains associated with this process, but
                    // a transient snapshot/loader error must not disable it for
                    // the lifetime of ShellHost. Check for an existing module
                    // again before retrying, so success never adds another ref.
                    if (!found->loaded && !found->thread && GetTickCount64() >= found->retryAfter) BeginInjection(*found);
                    continue;
                }
                Target target{};
                target.pid = entry.th32ProcessID;
                target.created = created;
                target.process = process;
                BeginInjection(target);
                targets.push_back(target);
                error = ERROR_SUCCESS;
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
};

NativeHookController::NativeHookController(std::wstring dllPath) : impl_(std::make_unique<Impl>(std::move(dllPath))) {}

NativeHookController::~NativeHookController() {
    PublishDisabled(impl_->state);
    for (auto& target : impl_->targets) {
        const bool completed = !target.thread || WaitForSingleObject(target.thread, 0) == WAIT_OBJECT_0;
        Impl::ReleaseTarget(target, completed);
    }
    if (impl_->state) UnmapViewOfFile(impl_->state);
    if (impl_->mapping) CloseHandle(impl_->mapping);
}

void NativeHookController::Enable(bool enabled) {
    impl_->enabled = enabled;
    PublishDisabled(impl_->state);
    if (!enabled) return;
    if (!impl_->OpenState()) return;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&impl_->state->enabled), 1);
    impl_->nextScan = 0;
    Tick();
}

void NativeHookController::RouteTo(HMONITOR monitor) {
    if (!impl_->enabled || !impl_->state) return;
    if (!monitor) {
        InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&impl_->state->validUntil), 0);
        return;
    }
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&impl_->state->enabled), 0);
    impl_->state->targetMonitorBounds = info.rcMonitor;
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&impl_->state->validUntil),
        static_cast<LONG64>(GetTickCount64() + winpplus::kRouteIntentLifetimeMs));
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&impl_->state->enabled), 1);
}

void NativeHookController::Tick() {
    impl_->Poll();
    if (!impl_->enabled) return;
    const ULONGLONG now = GetTickCount64();
    if (now < impl_->nextScan) return;
    impl_->nextScan = now + 2000;
    if (GetFileAttributesW(impl_->dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        impl_->error = ERROR_FILE_NOT_FOUND;
        return;
    }
    impl_->Scan();
}

bool NativeHookController::Ready() const {
    return impl_->enabled && impl_->state && std::any_of(impl_->targets.begin(), impl_->targets.end(),
        [](const Impl::Target& target) {
            return target.loaded && target.hookReady && !target.error && target.process && WaitForSingleObject(target.process, 0) == WAIT_TIMEOUT;
        });
}

DWORD NativeHookController::LastError() const {
    if (impl_->error) return impl_->error;
    for (const auto& target : impl_->targets) if (target.error) return target.error;
    return ERROR_SUCCESS;
}
