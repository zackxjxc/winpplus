#include "application.h"
#include "modern_ui.h"
#include "popup_surface.h"

#include <windowsx.h>

void Application::RepaintMenus() {
    for (HWND hwnd : menus)
        InvalidateRect(hwnd, nullptr, FALSE);
}

void Application::CloseMenu() {
    closingMenu = true;
    const auto windows = menus;
    menus.clear();
    menuWindow = nullptr;
    router.MenuClosed();
    hoverEnabled = false;
    hovered = -1;
    menuPreviewSession = false;
    for (HWND hwnd : windows) {
        if (IsWindow(hwnd)) {
            DestroyWindow(hwnd);
        }
    }
    closingMenu = false;
    storage.Log(L"MENU_CLOSED");
}

void Application::CommitSelection(bool releasedWin) {
    if (selected < 0 || selected > 3)
        return;
    // WM_DISPLAYCHANGE dismisses stale menus. Use the opening snapshot here so
    // releasing Win never waits for a graphics-driver query on the UI thread.
    if (releasedWin && selected == currentMode) {
        RepaintMenus();
        storage.Log(L"RELEASE_NO_CHANGE keep-menu");
        return;
    }
    const int target = selected;
    const bool visualPreview = menuPreviewSession;
    CloseMenu();
    if (visualPreview) {
        storage.Log(L"PREVIEW_SELECTION mode=" + std::to_wstring(target));
        return;
    }
    if (target == currentMode) {
        storage.Log(L"NO_OP mode=" + std::to_wstring(target));
        return;
    }
    if (dryRun) {
        storage.Log(L"DRY_RUN mode=" + std::to_wstring(target));
        return;
    }
    switch (displaySwitch.Start(target)) {
    case SwitchStart::Busy:
        SetStatus(Tr(L"显示配置正在切换，请稍候", L"Display configuration is changing"));
        break;
    case SwitchStart::Failed:
        SetStatus(Tr(L"无法启动显示切换", L"Could not start display switching"));
        break;
    case SwitchStart::Started:
        SetStatus(Tr(L"正在应用显示配置…", L"Applying display configuration…"));
        storage.Log(L"SWITCH_REQUEST mode=" + std::to_wstring(target));
        break;
    }
}

void Application::ShowMenu(bool visualPreview) {
    if ((!customMode && !visualPreview) || menuWindow)
        return;
    if (displaySwitch.Pending()) {
        router.MenuClosed();
        SetStatus(Tr(L"显示配置正在切换，请稍候", L"Display configuration is changing"));
        return;
    }
    menuPreviewSession = visualPreview;
    const auto display = QueryCurrentDisplayState();
    currentMode = display.mode;
    selected = currentMode < 0 ? 0 : currentMode;
    hovered = -1;
    hoverEnabled = false;
    GetCursorPos(&initialCursor);
    HMONITOR active = triggerMonitor ? triggerMonitor : ActiveMonitor(false);
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));
    openingMenu = true;
    for (HMONITOR monitor : monitors) {
        MONITORINFO info{sizeof(info)};
        if (!GetMonitorInfoW(monitor, &info))
            continue;
        const UINT dpi = DpiForMonitor(monitor);
        const int width = MulDiv(modern_ui::kMenuWidth, static_cast<int>(dpi), 96),
                  height = MulDiv(modern_ui::kMenuHeight, static_cast<int>(dpi), 96);
        const int x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2,
                  y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
        HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | (previewMenu ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW),
                                    kMenuClass, Tr(L"投屏", L"Project"), WS_POPUP | WS_THICKFRAME | WS_CAPTION,
                                    x, y, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), this);
        if (!hwnd) {
            continue;
        }
        if (!popup_surface::ApplyNativeFrame(hwnd))
            storage.Log(L"MENU native DWM frame unavailable");
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        menus.push_back(hwnd);
        if (monitor == active || !menuWindow)
            menuWindow = hwnd;
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd);
        storage.Log(L"MENU hwnd=" + std::to_wstring(reinterpret_cast<UINT_PTR>(hwnd)) + L" dpi=" +
                    std::to_wstring(dpi) + L" x=" + std::to_wstring(x) + L" y=" + std::to_wstring(y));
    }
    openingMenu = false;
    if (menuWindow) {
        SetForegroundWindow(menuWindow);
        SetFocus(menuWindow);
    } else
        router.MenuClosed();
    storage.Log(L"MENU_OPEN count=" + std::to_wstring(menus.size()) + L" current=" +
                std::to_wstring(currentMode) + L" " + display.summary);
}

void Application::ChangeSelection(int delta) {
    selected = (selected + delta + 4) % 4;
    hovered = -1;
    hoverEnabled = false;
    GetCursorPos(&initialCursor);
    RepaintMenus();
    storage.Log(L"SELECT " + std::to_wstring(selected));
}

LRESULT Application::MenuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCCALCSIZE:
        return 0;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_NCACTIVATE:
        return DefWindowProcW(hwnd, msg, wp, -1);
    case WM_DWMCOMPOSITIONCHANGED:
        popup_surface::ApplyNativeFrame(hwnd);
        RepaintMenus();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        modern_ui::DrawMenu(hwnd, dc, chinese, selected, currentMode, hovered, menuPreviewSession);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        POINT screen{};
        GetCursorPos(&screen);
        if (!hoverEnabled && screen.x == initialCursor.x && screen.y == initialCursor.y)
            return 0;
        hoverEnabled = true;
        const POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const int index = modern_ui::MenuHitTest(hwnd, point);
        const int nextHover = modern_ui::MenuCloseHitTest(hwnd, point) ? 4 : index;
        const bool changed = nextHover != hovered;
        hovered = nextHover;
        if (changed)
            RepaintMenus();
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        hovered = -1;
        RepaintMenus();
        return 0;
    case WM_LBUTTONUP: {
        const POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (modern_ui::MenuCloseHitTest(hwnd, point)) {
            CloseMenu();
            return 0;
        }
        const int index = modern_ui::MenuHitTest(hwnd, point);
        if (index >= 0) {
            selected = index;
            CommitSelection(false);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            CloseMenu();
            return 0;
        }
        if (wp == VK_UP || wp == VK_LEFT) {
            ChangeSelection(-1);
            return 0;
        }
        if (wp == VK_DOWN || wp == VK_RIGHT || wp == VK_TAB) {
            ChangeSelection(1);
            return 0;
        }
        if (wp == VK_RETURN || wp == VK_SPACE) {
            CommitSelection(false);
            return 0;
        }
        break;
    case WM_ACTIVATE:
        popup_surface::ApplyNativeFrame(hwnd);
        // Keep the explicit documentation preview open while a capture tool has focus.
        if (LOWORD(wp) == WA_INACTIVE && !previewMenu && !openingMenu && !closingMenu)
            PostMessageW(trayWindow, kFocusCheck, 0, 0);
        break;
    case WM_DPICHANGED: {
        const auto* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        RepaintMenus();
        return 0;
    }
    case WM_CLOSE:
        CloseMenu();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
