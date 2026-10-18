#include "../src/startup_registration.h"

#include <array>
#include <ostream>

namespace {
struct Fixture {
    std::wstring path = L"Software\\WinPPlus\\SelfTests\\" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    HKEY root{}, user{}, machine{};
    bool owned{};
    Fixture() {
        DWORD disposition{};
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
                            nullptr, &root, &disposition)) return;
        owned = disposition == REG_CREATED_NEW_KEY;
        if (owned) {
            RegCreateKeyExW(root, L"User", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &user, nullptr);
            RegCreateKeyExW(root, L"Machine", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &machine, nullptr);
        }
    }
    DWORD Clean() {
        if (user) {
            RegDeleteKeyValueW(user, startup_registry::kApproval, startup_registry::kName);
            RegDeleteKeyValueW(user, startup_registry::kRun, startup_registry::kName);
            RegDeleteKeyValueW(user, startup_registry::kPolicy, L"DisableCurrentUserRun");
        }
        if (machine) RegDeleteKeyValueW(machine, startup_registry::kPolicy, L"DisableCurrentUserRun");
        if (user) { RegCloseKey(user); user = nullptr; }
        if (machine) { RegCloseKey(machine); machine = nullptr; }
        const DWORD result = owned && root ? RegDeleteTreeW(root, nullptr) : ERROR_SUCCESS;
        if (root) { RegCloseKey(root); root = nullptr; }
        if (result) return result;
        const DWORD removed = owned ? RegDeleteKeyW(HKEY_CURRENT_USER, path.c_str()) : ERROR_SUCCESS;
        owned = false;
        return removed;
    }
    ~Fixture() { Clean(); }
    bool Put(HKEY hive, const wchar_t* keyPath, const wchar_t* name, DWORD type, const void* data, DWORD size) {
        HKEY key{};
        if (RegCreateKeyExW(hive, keyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)) return false;
        const DWORD result = RegSetValueExW(key, name, 0, type, static_cast<const BYTE*>(data), size);
        RegCloseKey(key);
        return result == ERROR_SUCCESS;
    }
    bool Command(const std::wstring& command) {
        return Put(user, startup_registry::kRun, startup_registry::kName, REG_SZ,
                   command.c_str(), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    }
};
}

int RunStartupTests(std::ostream& report) {
    int failures{};
    auto check = [&](bool ok, const char* name) {
        report << (ok ? "PASS " : "FAIL ") << name << "\n";
        if (!ok) ++failures;
    };
    Fixture f;
    check(f.owned && f.user && f.machine, "isolated startup registry fixture created");
    if (!f.user || !f.machine) return failures;
    StartupRegistration startup(f.user, f.machine);
    auto state = [&] { return startup.Inspect().state; };
    check(state() == StartupState::NotRegistered, "missing startup registration is unchecked");
    check(startup.SetEnabled(true) == ERROR_SUCCESS && startup.Inspect().Enabled(),
          "startup enable writes and verifies current command");
    const auto valid = startup.Inspect().command;
    check(valid.starts_with(L"\"") && valid.ends_with(L"\" --background"), "startup executable path is quoted");
    check(f.Command(L"relative.exe --background") && state() == StartupState::InvalidCommand,
          "startup relative path is rejected");
    check(f.Command(valid + L" --self-test") && state() == StartupState::InvalidCommand,
          "startup incorrect arguments are rejected");
    check(f.Command(L"\"C:\\WinPPlus-missing-test.exe\" --background") &&
          state() == StartupState::MissingExecutable, "startup missing executable is unchecked");
    check(startup.SetEnabled(true) == ERROR_SUCCESS && startup.Inspect().Enabled(),
          "startup explicit enable repairs stale command");
    // A second valid Windows executable must not count as this app's startup.
    wchar_t system[32768]{};
    GetSystemDirectoryW(system, ARRAYSIZE(system));
    check(f.Command(L"\"" + std::wstring(system) + L"\\notepad.exe\" --background") &&
          state() == StartupState::DifferentExecutable, "startup other executable is unchecked");
    startup.SetEnabled(true);
    for (DWORD flag : {2UL, 3UL, 6UL, 7UL, 99UL}) {
        const std::array<DWORD, 3> approval{flag, 0, 0};
        const auto expected = flag == 2 || flag == 6 ? StartupState::Enabled :
            flag == 3 || flag == 7 ? StartupState::DisabledByWindows : StartupState::Unknown;
        check(f.Put(f.user, startup_registry::kApproval, startup_registry::kName, REG_BINARY,
                    approval.data(), sizeof(approval)) && state() == expected,
              "startup Windows approval state reflected correctly");
        if (flag == 3 || flag == 7)
            check(startup.SetEnabled(true) == ERROR_ACCESS_DISABLED_BY_POLICY && state() == expected,
                  "startup does not override Windows disabled state");
    }
    const DWORD malformed = 2;
    check(f.Put(f.user, startup_registry::kApproval, startup_registry::kName, REG_BINARY,
                &malformed, sizeof(malformed)) && state() == StartupState::Unknown,
          "startup unknown approval format is unchecked");
    check(startup.SetEnabled(false) == ERROR_SUCCESS && state() == StartupState::NotRegistered,
          "startup disable deletes Run entry and verifies result");
    const DWORD disabled = 1;
    for (HKEY hive : {f.user, f.machine})
        check(f.Put(hive, startup_registry::kPolicy, L"DisableCurrentUserRun", REG_DWORD, &disabled, sizeof(disabled)) &&
              state() == StartupState::DisabledByPolicy && startup.SetEnabled(true) == ERROR_ACCESS_DISABLED_BY_POLICY,
              "startup policy prevents false enabled state");
    StartupRegistration invalid(reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(1)), f.machine);
    check(invalid.Inspect().state == StartupState::Unknown, "startup registry failure is unchecked");
    const DWORD cleaned = f.Clean();
    if (cleaned != ERROR_SUCCESS)
        report << "NOTE empty startup fixture keys could not be removed, error=" << cleaned << "\n";
    return failures;
}
