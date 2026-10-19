#include "native_hook_controller.h"
#include "native_route_state.h"

#include <tlhelp32.h>
#include <dwmapi.h>
#include <ole2.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <system_error>
#include <thread>
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
    struct LegacyMetrics {
        HWND window{};
        DWORD pid{};
        UINT dpi{};
        int width{};
        DWORD error{};
        ULONGLONG started{};
        std::atomic<bool> done{};

        void Read() {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(initialized)) { error = static_cast<DWORD>(initialized); return; }
            {
                using Microsoft::WRL::ComPtr;
                ComPtr<IUIAutomation> automation;
                ComPtr<IUIAutomationElement> root, frame;
                ComPtr<IUIAutomationCondition> condition;
                HRESULT result = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                                  IID_PPV_ARGS(&automation));
                if (SUCCEEDED(result)) result = automation->ElementFromHandle(window, &root);
                VARIANT id{};
                id.vt = VT_BSTR;
                id.bstrVal = SysAllocString(L"DevicesFlowFrame");
                if (SUCCEEDED(result))
                    result = id.bstrVal ? automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, &condition)
                                        : E_OUTOFMEMORY;
                VariantClear(&id);
                if (SUCCEEDED(result)) result = root->FindFirst(TreeScope_Children, condition.Get(), &frame);
                RECT bounds{};
                if (SUCCEEDED(result)) result = frame ? frame->get_CurrentBoundingRectangle(&bounds) : UIA_E_ELEMENTNOTAVAILABLE;
                if (SUCCEEDED(result) && bounds.right > bounds.left) {
                    width = bounds.right - bounds.left;
                    dpi = GetDpiForWindow(window);
                } else {
                    error = FAILED(result) ? static_cast<DWORD>(result) : ERROR_INVALID_DATA;
                }
            }
            CoUninitialize();
        }
    };

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
    DWORD legacyShellPid{};
    RECT legacyBounds{};
    ULONGLONG legacyPendingUntil{};
    HWND legacyWindow{}, legacyWidthWindow{};
    int legacyNativeWidth{};
    UINT legacyMeasuredDpi{};
    std::shared_ptr<LegacyMetrics> legacyMetrics;
    ULONGLONG nextLegacyMetrics{};
    DWORD legacyError{};

    bool IsLegacyProject(HWND window) const {
        DWORD pid{};
        GetWindowThreadProcessId(window, &pid);
        if (!legacyShellPid || pid != legacyShellPid)
            return false;
        wchar_t cls[128]{}, title[128]{};
        GetClassNameW(window, cls, ARRAYSIZE(cls));
        GetWindowTextW(window, title, ARRAYSIZE(title));
        return wcscmp(cls, L"Windows.UI.Core.CoreWindow") == 0 &&
               (wcscmp(title, L"投影") == 0 || wcscmp(title, L"Project") == 0);
    }

    void CancelLegacyRoute() {
        legacyPendingUntil = 0;
        legacyWindow = nullptr;
        // A verified content width remains useful for the next opening. HWND,
        // process and window DPI are checked before it can be reused.
        nextLegacyMetrics = 0;
        legacyError = ERROR_SUCCESS;
    }

    bool ReadLegacyWidth(HWND window) {
        const auto cached = [&] {
            return legacyWidthWindow == window && legacyMeasuredDpi == GetDpiForWindow(window);
        };
        if (legacyMetrics) {
            if (!legacyMetrics->done.load(std::memory_order_acquire)) {
                if (GetTickCount64() - legacyMetrics->started > 2000) legacyError = WAIT_TIMEOUT;
                return cached();
            }
            if (legacyMetrics->window == window && legacyMetrics->pid == legacyShellPid) {
                legacyError = legacyMetrics->error;
                if (!legacyError && legacyMetrics->width > 0) {
                    legacyNativeWidth = legacyMetrics->width;
                    legacyMeasuredDpi = legacyMetrics->dpi;
                    legacyWidthWindow = window;
                    nextLegacyMetrics = GetTickCount64() + 1000;
                } else {
                    nextLegacyMetrics = GetTickCount64() + 500;
                }
            }
            legacyMetrics.reset();
        }
        if (GetTickCount64() < nextLegacyMetrics) return cached();
        auto metrics = std::make_shared<LegacyMetrics>();
        metrics->window = window;
        metrics->pid = legacyShellPid;
        metrics->started = GetTickCount64();
        try {
            // An out-of-process accessibility provider may block. The worker
            // owns only its result, never the controller or the UI thread.
            std::thread([metrics] {
                metrics->Read();
                metrics->done.store(true, std::memory_order_release);
            }).detach();
            legacyMetrics = std::move(metrics);
        } catch (const std::system_error&) {
            legacyError = ERROR_NOT_ENOUGH_MEMORY;
            nextLegacyMetrics = GetTickCount64() + 2000;
        }
        return cached();
    }

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

    void PositionLegacyFlyout(HWND window, const MONITORINFO& info) {
        RECT rect{};
        if (!GetWindowRect(window, &rect)) return;
        const int width = std::min(legacyNativeWidth, static_cast<int>(info.rcWork.right - info.rcWork.left));
        const int height = info.rcWork.bottom - info.rcWork.top;
        const int x = info.rcWork.right - width, y = info.rcWork.top;
        if (rect.left != x || rect.top != y || rect.right - rect.left != width || rect.bottom - rect.top != height)
            legacyError = SetWindowPos(window, nullptr, x, y, width, height,
                                      SWP_NOACTIVATE | SWP_NOZORDER | SWP_ASYNCWINDOWPOS)
                              ? ERROR_SUCCESS : GetLastError();
    }

    void RouteLegacyFlyout() {
        if (!legacyShellPid || !state || !state->enabled || !legacyPendingUntil)
            return;
        if (legacyWindow) {
            DWORD cloaked{};
            if (!IsLegacyProject(legacyWindow) || !IsWindowVisible(legacyWindow) ||
                FAILED(DwmGetWindowAttribute(legacyWindow, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) || cloaked) {
                CancelLegacyRoute();
                return;
            }
        } else if (GetTickCount64() >= legacyPendingUntil) {
            CancelLegacyRoute();
            return;
        }
        HMONITOR target = MonitorFromRect(&legacyBounds, MONITOR_DEFAULTTONULL);
        MONITORINFO info{sizeof(info)};
        if (!target || !GetMonitorInfoW(target, &info) || !EqualRect(&legacyBounds, &info.rcMonitor)) {
            CancelLegacyRoute();
            return;
        }
        // EnumWindows omits immersive windows on Windows 10. FindWindowEx also
        // sees the projection CoreWindow; never move other Shell flyouts.
        HWND window{};
        while ((window = FindWindowExW(nullptr, window, L"Windows.UI.Core.CoreWindow", nullptr))) {
            if ((legacyWindow && window != legacyWindow) || !IsLegacyProject(window) || !IsWindowVisible(window))
                continue;
            DWORD cloaked{};
            if (FAILED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) || cloaked)
                continue;
            RECT rect{};
            if (!GetWindowRect(window, &rect) || rect.right <= rect.left || rect.bottom <= rect.top)
                continue;
            legacyWindow = window;
            // CoreWindow can be wider than the actual XAML content, including
            // when a previous version already resized it. Read DevicesFlowFrame
            // instead of trusting a cached outer rectangle or monitor DPI.
            if (!ReadLegacyWidth(window)) return;
            PositionLegacyFlyout(window, info);
        }
    }

    bool LegacyReady() const {
        if (!legacyShellPid)
            return false;
        HWND window{};
        while ((window = FindWindowExW(nullptr, window, L"Windows.UI.Core.CoreWindow", nullptr))) {
            DWORD pid{};
            GetWindowThreadProcessId(window, &pid);
            if (pid != legacyShellPid)
                continue;
            wchar_t title[128]{};
            GetWindowTextW(window, title, ARRAYSIZE(title));
            if (wcscmp(title, L"投影") == 0 || wcscmp(title, L"Project") == 0)
                return true;
        }
        return false;
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
        const DWORD previousLegacyPid = legacyShellPid;
        legacyShellPid = 0;
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
                const bool legacy = _wcsicmp(entry.szExeFile, L"ShellExperienceHost.exe") == 0;
                if (!legacy && _wcsicmp(entry.szExeFile, L"ShellHost.exe") != 0) continue;
                DWORD targetSession{};
                if (!ProcessIdToSessionId(entry.th32ProcessID, &targetSession) || targetSession != session) continue;
                if (legacy) {
                    legacyShellPid = entry.th32ProcessID;
                    continue;
                }
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
        if (previousLegacyPid != legacyShellPid) {
            legacyWindow = nullptr;
            legacyWidthWindow = nullptr;
        }
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
    impl_->CancelLegacyRoute();
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
        impl_->CancelLegacyRoute();
        return;
    }
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return;
    impl_->legacyBounds = info.rcMonitor;
    impl_->legacyPendingUntil = GetTickCount64() + 5000;
    if (impl_->legacyWidthWindow && impl_->IsLegacyProject(impl_->legacyWidthWindow) &&
        impl_->legacyMeasuredDpi == GetDpiForWindow(impl_->legacyWidthWindow)) {
        // Place an existing flyout before forwarding Win+P. Shell may still
        // override its position during activation, so keep correcting afterward.
        impl_->PositionLegacyFlyout(impl_->legacyWidthWindow, info);
    }
    // Keep a bound, visible flyout bound through the shell's later layout passes.
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&impl_->state->enabled), 0);
    impl_->state->targetMonitorBounds = info.rcMonitor;
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&impl_->state->validUntil),
        static_cast<LONG64>(GetTickCount64() + winpplus::kRouteIntentLifetimeMs));
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&impl_->state->enabled), 1);
}

void NativeHookController::Tick() {
    impl_->Poll();
    if (!impl_->enabled) return;
    impl_->RouteLegacyFlyout();
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
    return impl_->enabled && impl_->state && ((impl_->LegacyReady() && !impl_->legacyError) || std::any_of(impl_->targets.begin(), impl_->targets.end(),
        [](const Impl::Target& target) {
            return target.loaded && target.hookReady && !target.error && target.process && WaitForSingleObject(target.process, 0) == WAIT_TIMEOUT;
        }));
}

DWORD NativeHookController::LastError() const {
    if (impl_->legacyError) return impl_->legacyError;
    if (impl_->error) return impl_->error;
    for (const auto& target : impl_->targets) if (target.error) return target.error;
    return ERROR_SUCCESS;
}

bool NativeHookController::IsProjectionWindow(HWND window) const {
    if (impl_->IsLegacyProject(window))
        return true;
    wchar_t cls[128]{};
    GetClassNameW(window, cls, ARRAYSIZE(cls));
    if (wcscmp(cls, L"ControlCenterWindow") != 0)
        return false;
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    return std::any_of(impl_->targets.begin(), impl_->targets.end(),
                       [pid](const Impl::Target& target) { return target.pid == pid; });
}
