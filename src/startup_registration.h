#pragma once

#include <windows.h>
#include <filesystem>
#include <string>
#include "app_identity.h"

namespace startup_registry {
inline constexpr wchar_t kRun[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kApproval[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
inline constexpr wchar_t kPolicy[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer";
inline constexpr auto kName = winpplus::kName;
}

enum class StartupState {
    NotRegistered, Enabled, InvalidCommand, MissingExecutable, DifferentExecutable,
    DisabledByWindows, DisabledByPolicy, Unknown
};

struct StartupInfo {
    StartupState state = StartupState::NotRegistered;
    DWORD error{};
    std::wstring command;
    bool Enabled() const { return state == StartupState::Enabled; }
    bool operator==(const StartupInfo&) const = default;
};

// Borrowed hive roots allow integration tests without changing real startup settings.
class StartupRegistration final {
  public:
    explicit StartupRegistration(HKEY user = HKEY_CURRENT_USER, HKEY machine = HKEY_LOCAL_MACHINE,
                                 std::filesystem::path executable = {});
    StartupInfo Inspect() const;
    DWORD SetEnabled(bool enabled) const;

  private:
    HKEY user_, machine_;
    std::filesystem::path executable_;
};
