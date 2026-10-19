#include "application.h"
#include "modern_ui.h"

#include <shellapi.h>
#include <algorithm>

void Application::AddTrayIcon() {
    DWORD lightTheme{}, size = sizeof(lightTheme);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &lightTheme, &size);
    HICON next = modern_ui::CreateAppIcon(GetSystemMetrics(SM_CXSMICON), lightTheme == 0);
    if (!next)
        return;
    NOTIFYICONDATAW icon{sizeof(icon)};
    icon.hWnd = trayWindow;
    icon.uID = 1;
    icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    icon.uCallbackMessage = kTrayEvent;
    icon.hIcon = next;
    wcscpy_s(icon.szTip, winpplus::kTrayTip);
    if (Shell_NotifyIconW(trayIconAdded ? NIM_MODIFY : NIM_ADD, &icon)) {
        if (trayIcon)
            DestroyIcon(trayIcon);
        trayIcon = next;
        trayIconAdded = true;
    } else {
        DestroyIcon(next);
    }
}

LRESULT Application::TrayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (msg == taskbarCreated) {
        trayIconAdded = false;
        AddTrayIcon();
        return 0;
    }
    if (msg == WM_SETTINGCHANGE || msg == WM_THEMECHANGED) {
        AddTrayIcon();
        return 0;
    }
    if (msg == kShowSettings) {
        ShowSettings();
        return 0;
    }
    if (msg == kKeyAction) {
        switch (static_cast<KeyAction>(wp)) {
        case KeyAction::Open:
            ShowMenu();
            break;
        case KeyAction::Cycle:
            if (menuWindow)
                ChangeSelection(1);
            else
                ShowMenu();
            break;
        case KeyAction::Commit:
            if (menuWindow)
                CommitSelection(true);
            break;
        default:
            break;
        }
        return 0;
    }
    if (msg == kFocusCheck) {
        HWND focus = GetForegroundWindow();
        if (menuWindow && std::find(menus.begin(), menus.end(), focus) == menus.end())
            CloseMenu();
        return 0;
    }
    if (msg == WM_TIMER) {
        if (wp == 2) {
            if (nativeHook && optimizeMode && GetTickCount64() < nativeFastUntil)
                nativeHook->Tick();
            else
                KillTimer(hwnd, 2);
        } else {
            Tick();
        }
        return 0;
    }
    if (msg == WM_DISPLAYCHANGE) {
        if (menuWindow)
            CloseMenu();
        RefreshSettings();
        return 0;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == 1)
            ShowSettings();
        if (LOWORD(wp) == 2)
            PostQuitMessage(0);
        return 0;
    }
    if (msg == kTrayEvent) {
        if (lp == WM_LBUTTONDBLCLK)
            ShowSettings();
        else if (lp == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, Tr(L"显示窗口", L"Show window"));
            AppendMenuW(menu, MF_STRING, 2, Tr(L"退出", L"Exit"));
            POINT p{};
            GetCursorPos(&p);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            PostMessageW(hwnd, WM_NULL, 0, 0);
        }
        return 0;
    }
    if (msg == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
