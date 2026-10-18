#include "startup_registration.h"

#include <shellapi.h>
#include <array>

namespace {
bool Missing(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

DWORD Read(HKEY root, const wchar_t* key, const wchar_t* name, DWORD& type, void* data, DWORD& size) {
    return RegGetValueW(root, key, name, RRF_RT_ANY | RRF_NOEXPAND, &type, data, &size);
}

DWORD Policy(HKEY user, HKEY machine) {
    for (HKEY hive : {user, machine}) {
        DWORD disabled{}, type{}, size = sizeof(disabled);
        const DWORD error = Read(hive, startup_registry::kPolicy, L"DisableCurrentUserRun", type, &disabled, size);
        if (Missing(error)) continue;
        if (error) return error;
        if (type != REG_DWORD || size != sizeof(disabled)) return ERROR_INVALID_DATA;
        if (disabled) return ERROR_ACCESS_DISABLED_BY_POLICY;
    }
    return ERROR_SUCCESS;
}
}

StartupRegistration::StartupRegistration(HKEY user, HKEY machine, std::filesystem::path executable)
    : user_(user), machine_(machine), executable_(std::move(executable)) {
    if (executable_.empty()) {
        wchar_t path[32768]{};
        const DWORD size = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
        if (size && size < ARRAYSIZE(path)) executable_ = path;
    }
}

StartupInfo StartupRegistration::Inspect() const {
    StartupInfo info;
    auto fail = [&](StartupState state, DWORD error = ERROR_INVALID_DATA) {
        info.state = state;
        info.error = error;
        return info;
    };
    const DWORD policy = Policy(user_, machine_);
    if (policy)
        return fail(policy == ERROR_ACCESS_DISABLED_BY_POLICY ? StartupState::DisabledByPolicy : StartupState::Unknown, policy);

    // Run commands have a documented 260-character limit. Accept the absolute
    // EXE + --background form written by this application.
    wchar_t command[262]{};
    DWORD type{}, size = sizeof(command);
    const DWORD error = Read(user_, startup_registry::kRun, startup_registry::kName, type, command, size);
    if (Missing(error)) return info;
    if (error) return fail(StartupState::Unknown, error);
    if (type != REG_SZ || size < sizeof(wchar_t) || size % sizeof(wchar_t))
        return fail(StartupState::InvalidCommand);
    info.command = command;
    if (info.command.empty() || info.command.size() > 260 || size != (info.command.size() + 1) * sizeof(wchar_t))
        return fail(StartupState::InvalidCommand);
    int count{};
    LPWSTR* args = CommandLineToArgvW(command, &count);
    std::filesystem::path path;
    if (args && count == 2 && wcscmp(args[1], L"--background") == 0) path = args[0];
    if (args) LocalFree(args);
    if (!path.is_absolute()) return fail(StartupState::InvalidCommand);
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return fail(StartupState::MissingExecutable, ERROR_FILE_NOT_FOUND);
    if (executable_.empty()) return fail(StartupState::Unknown);
    if (_wcsicmp(path.c_str(), executable_.c_str()) != 0)
        return fail(StartupState::DifferentExecutable);

    // Explorer's approval format is an undocumented compatibility detail.
    // Recognize known flags, fail closed on others, and never modify this data.
    std::array<DWORD, 3> approval{};
    size = sizeof(approval);
    const DWORD approved = Read(user_, startup_registry::kApproval, startup_registry::kName, type, approval.data(), size);
    if (!Missing(approved)) {
        if (approved) return fail(StartupState::Unknown, approved);
        if (type != REG_BINARY || size != sizeof(approval)) return fail(StartupState::Unknown);
        if (approval[0] == 3 || approval[0] == 7)
            return fail(StartupState::DisabledByWindows, ERROR_ACCESS_DISABLED_BY_POLICY);
        if (approval[0] != 2 && approval[0] != 6) return fail(StartupState::Unknown);
    }
    info.state = StartupState::Enabled;
    return info;
}

DWORD StartupRegistration::SetEnabled(bool enabled) const {
    if (enabled) {
        const DWORD policy = Policy(user_, machine_);
        if (policy) return policy;
    }
    const std::wstring command = L"\"" + executable_.wstring() + L"\" --background";
    if (enabled && (!executable_.is_absolute() || command.size() > 260 ||
                    GetFileAttributesW(executable_.c_str()) == INVALID_FILE_ATTRIBUTES))
        return ERROR_INVALID_PARAMETER;
    HKEY key{};
    DWORD result = RegCreateKeyExW(user_, startup_registry::kRun, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result) return result;
    result = enabled
        ? RegSetValueExW(key, startup_registry::kName, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(command.c_str()),
                        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)))
        : RegDeleteValueW(key, startup_registry::kName);
    RegCloseKey(key);
    if (!enabled && Missing(result)) result = ERROR_SUCCESS;
    if (result) return result;
    const auto actual = Inspect();
    if (enabled ? actual.Enabled() : actual.state == StartupState::NotRegistered) return ERROR_SUCCESS;
    return actual.error ? actual.error : ERROR_INVALID_DATA;
}
