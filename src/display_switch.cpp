#include "display_switch.h"

#include <array>
#include <filesystem>

DisplaySwitch::~DisplaySwitch() {
    Reset();
}

void DisplaySwitch::Reset() {
    if (process_)
        CloseHandle(process_);
    process_ = nullptr;
    target_ = -1;
}

SwitchStart DisplaySwitch::Start(int mode) {
    if (process_)
        return SwitchStart::Busy;
    constexpr std::array<const wchar_t*, 4> arguments{L"/internal", L"/clone", L"/extend", L"/external"};
    if (mode < 0 || mode >= static_cast<int>(arguments.size()))
        return SwitchStart::Failed;
    wchar_t folder[MAX_PATH]{};
    GetSystemDirectoryW(folder, ARRAYSIZE(folder));
    const auto exe = (std::filesystem::path(folder) / L"DisplaySwitch.exe").wstring();
    std::wstring command = L"\"" + exe + L"\" " + arguments[static_cast<size_t>(mode)];
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                        &process))
        return SwitchStart::Failed;
    CloseHandle(process.hThread);
    process_ = process.hProcess;
    target_ = mode;
    started_ = GetTickCount64();
    return SwitchStart::Started;
}

SwitchUpdate DisplaySwitch::Poll(const DisplayState& actual) {
    if (!process_)
        return SwitchUpdate::None;
    DWORD code{};
    const bool exited = GetExitCodeProcess(process_, &code) && code != STILL_ACTIVE;
    if (actual.mode == target_) {
        Reset();
        return SwitchUpdate::Confirmed;
    }
    if ((exited && code != 0) || GetTickCount64() - started_ > 12000) {
        Reset();
        return SwitchUpdate::Unconfirmed;
    }
    return SwitchUpdate::None;
}
