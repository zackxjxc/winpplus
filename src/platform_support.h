#pragma once

#include <windows.h>
#include <VersionHelpers.h>

// Runs before Application construction, configuration access or hooks.
inline bool RequireWindows10(bool supported = IsWindows10OrGreater(),
                             decltype(&MessageBoxW) notify = &MessageBoxW) {
    if (supported) return true;
    const bool chinese = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
    notify(nullptr, chinese ? L"仅限 Windows 10 及以上系统使用。" : L"Windows 10 or later is required.",
           L"WinPPlus", MB_OK | MB_ICONINFORMATION);
    return false;
}
