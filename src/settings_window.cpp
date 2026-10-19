#include "application.h"
#include "modern_ui.h"
#include "app_version.h"

#include <windowsx.h>
#include <shellapi.h>

namespace {
void SetLabel(HWND hwnd, const wchar_t* text) {
    wchar_t previous[256]{};
    GetWindowTextW(hwnd, previous, ARRAYSIZE(previous));
    if (wcscmp(previous, text) != 0)
        SetWindowTextW(hwnd, text);
}
}

void Application::RefreshSettings() {
    if (!settingsWindow)
        return;
    const std::wstring title = std::wstring(Tr(winpplus::kSettingsTitleZh, winpplus::kSettingsTitleEn)) +
                               L" · v" + kAppVersion;
    SetLabel(settingsWindow, title.c_str());
    SetLabel(GetDlgItem(settingsWindow, 100), Tr(L"使用系统默认菜单", L"Use the default Windows menu"));
    SetLabel(GetDlgItem(settingsWindow, 101),
                   Tr(L"让原生菜单跟随活动屏幕", L"Follow the active display"));
    SetLabel(GetDlgItem(settingsWindow, 102),
                   Tr(L"使用自定义居中菜单", L"Use the centered custom menu"));
    SetLabel(GetDlgItem(settingsWindow, 103), Tr(L"随 Windows 启动", L"Start with Windows"));
    SetLabel(GetDlgItem(settingsWindow, 104), Tr(L"简体中文", L"English"));
    SetLabel(GetDlgItem(settingsWindow, 106), Tr(L"显示托盘图标", L"Show tray icon"));
    InvalidateRect(settingsWindow, nullptr, FALSE);
}

void Application::RefreshStartupState() {
    if (selfTest)
        return;
    nextStartupCheck = GetTickCount64() + 1000;
    const auto actual = startup.Inspect();
    if (actual != startupInfo) {
        startupInfo = actual;
        RefreshSettings();
    }
}

const wchar_t* Application::StartupHint() const {
    switch (startupInfo.state) {
    case StartupState::InvalidCommand:
        return Tr(L"启动命令无效，点击开机启动可修复。", L"Invalid startup command. Click Start with Windows to repair.");
    case StartupState::MissingExecutable:
        return Tr(L"启动文件不可用，点击开机启动可修复。", L"Startup file is unavailable. Click Start with Windows to repair.");
    case StartupState::DifferentExecutable:
        return Tr(L"启动路径指向其他副本，点击可登记当前程序。", L"Startup points to another copy. Click to register this app.");
    case StartupState::DisabledByWindows:
        return Tr(L"已被 Windows 禁用，点击开机启动打开系统设置。", L"Disabled by Windows. Click Start with Windows to open system settings.");
    case StartupState::DisabledByPolicy:
        return Tr(L"开机启动被系统策略禁用。", L"Startup is disabled by system policy.");
    case StartupState::Unknown:
        return Tr(L"无法确认开机启动状态，未标记为启用。", L"Startup status cannot be verified; it is not marked enabled.");
    default:
        return L"";
    }
}

void Application::SetSettingsHover(int id) {
    if (id == 103 || id == 106)
        id = -1;
    if (settingsHover == id)
        return;
    const int previous = settingsHover;
    settingsHover = id;
    for (int changed : {previous, id}) {
        if (changed < 100 || changed > 104)
            continue;
        RECT rect = modern_ui::GetSettingsControlRect(settingsWindow, changed);
        InvalidateRect(settingsWindow, &rect, FALSE);
    }
}

void Application::ActivateSetting(int id) {
    if (id == 100) {
        optimizeMode = false;
        customMode = false;
        ConfigureModes();
    } else if (id == 101) {
        optimizeMode = !optimizeMode;
        customMode = false;
        ConfigureModes();
    } else if (id == 102) {
        customMode = !customMode;
        optimizeMode = false;
        ConfigureModes();
    } else if (id == 103) {
        RefreshStartupState();
        const DWORD result = startup.SetEnabled(!startupInfo.Enabled());
        RefreshStartupState();
        if (startupInfo.state == StartupState::DisabledByWindows) {
            const auto opened = reinterpret_cast<INT_PTR>(ShellExecuteW(settingsWindow, L"open",
                L"ms-settings:startupapps", nullptr, nullptr, SW_SHOWNORMAL));
            if (opened <= 32)
                SetStatus(Tr(L"请在任务管理器的启动应用中启用本程序。", L"Enable this app in Task Manager's Startup apps."));
        } else if (result != ERROR_SUCCESS) {
            SetStatus(Tr(L"开机启动设置失败", L"Could not update startup setting"));
        }
    } else if (id == 106) {
        showTrayIcon = !showTrayIcon;
        AddTrayIcon();
    } else if (id == 104) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (chinese ? MF_CHECKED : 0), 1, L"简体中文");
        AppendMenuW(menu, MF_STRING | (!chinese ? MF_CHECKED : 0), 2, L"English");
        RECT rect{};
        GetWindowRect(GetDlgItem(settingsWindow, 104), &rect);
        const UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN, rect.right,
                                           rect.bottom, 0, settingsWindow, nullptr);
        DestroyMenu(menu);
        if (!choice)
            return;
        chinese = choice == 1;
        CloseMenu();
        SetStatus(customMode     ? Tr(L"自定义菜单已启用", L"Custom menu enabled")
                  : optimizeMode ? Tr(L"系统菜单优化已启用", L"System menu optimization enabled")
                                 : Tr(L"使用 Windows 默认菜单", L"Using the default Windows menu"));
    }
    SaveConfig();
    RefreshSettings();
}

void Application::LayoutControls(HWND hwnd) {
    for (int id : kSettingsControls) {
        RECT r = modern_ui::GetSettingsControlRect(hwnd, id);
        SetWindowPos(GetDlgItem(hwnd, id), nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOZORDER);
    }
}

LRESULT Application::SettingButtonProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // These native buttons retain keyboard/accessibility behavior. All pixels
    // belong to the parent's single buffered paint, avoiding competing frames.
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (msg == WM_MOUSEMOVE) {
        SetSettingsHover(GetDlgCtrlID(hwnd));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
    } else if (msg == WM_MOUSELEAVE) {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(GetParent(hwnd), &point);
        SetSettingsHover(modern_ui::SettingsHitTest(GetParent(hwnd), point));
    }
    const LRESULT result = CallWindowProcW(buttonProc, hwnd, msg, wp, lp);
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_UPDATEUISTATE) {
        RECT rect = modern_ui::GetSettingsControlRect(GetParent(hwnd), GetDlgCtrlID(hwnd));
        InvalidateRect(GetParent(hwnd), &rect, FALSE);
    }
    return result;
}

LRESULT Application::SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        for (int id : kSettingsControls) {
            HWND control = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0,
                                         0, 1, 1, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                         GetModuleHandleW(nullptr), this);
            buttonProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(control, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SettingButtonCallback)));
        }
        LayoutControls(hwnd);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        modern_ui::DrawSettings(hwnd, dc, chinese, optimizeMode, customMode, startupInfo.Enabled(), settingsHover,
                                statusText, StartupHint(), showTrayIcon);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        const int id = static_cast<int>(draw->CtlID);
        RECT r = modern_ui::GetSettingsControlRect(hwnd, id);
        InvalidateRect(hwnd, &r, FALSE);
        return TRUE;
    }
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED) {
            ActivateSetting(LOWORD(wp));
            return 0;
        }
        break;
    case WM_MOUSEMOVE: {
        const int hit = modern_ui::SettingsHitTest(hwnd, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        SetSettingsHover(hit);
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE: {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(hwnd, &point);
        SetSettingsHover(modern_ui::SettingsHitTest(hwnd, point));
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        LayoutControls(hwnd);
        return 0;
    case WM_DPICHANGED: {
        const auto* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutControls(hwnd);
        RefreshSettings();
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE)
            RefreshStartupState();
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void Application::ShowSettings() {
    if (!settingsWindow) {
        HMONITOR monitor = ActiveMonitor(false);
        MONITORINFO info{sizeof(info)};
        GetMonitorInfoW(monitor, &info);
        const UINT dpi = DpiForMonitor(monitor);
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT r{0, 0, MulDiv(modern_ui::kSettingsWidth, static_cast<int>(dpi), 96),
               MulDiv(modern_ui::kSettingsHeight, static_cast<int>(dpi), 96)};
        AdjustWindowRectExForDpi(&r, style, FALSE, WS_EX_APPWINDOW, dpi);
        const int width = r.right - r.left, height = r.bottom - r.top,
                  x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2,
                  y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
        settingsWindow = CreateWindowExW(
            WS_EX_APPWINDOW, kSettingsClass, Tr(winpplus::kSettingsTitleZh, winpplus::kSettingsTitleEn),
            style, x, y, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), this);
        RoundWindow(settingsWindow);
        SendMessageW(settingsWindow, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
    }
    RefreshStartupState();
    RefreshSettings();
    ShowWindow(settingsWindow, SW_SHOWNORMAL);
    SetForegroundWindow(settingsWindow);
}
