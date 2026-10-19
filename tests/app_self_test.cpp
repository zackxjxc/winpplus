#include "application.h"
#include "modern_ui.h"
#include "app_version.h"
#include "platform_support.h"

#include <algorithm>
#include <dwmapi.h>
#include <windowsx.h>
#include <fstream>

int RunStartupTests(std::ostream& report);

int Application::RunSelfTests() {
    std::ofstream report(storage.Root() / L"self-test.txt");
    if (!report)
        return 3;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        report << (ok ? "PASS " : "FAIL ") << name << "\n";
        if (!ok)
            ++failures;
    };
    static int versionNotices{};
    static bool validVersionNotice{};
    versionNotices = 0;
    validVersionNotice = false;
    const auto notice = +[](HWND owner, LPCWSTR message, LPCWSTR title, UINT flags) -> int {
        ++versionNotices;
        validVersionNotice = owner == nullptr && wcscmp(title, L"WinPPlus") == 0 &&
            (wcscmp(message, L"仅限 Windows 10 及以上系统使用。") == 0 ||
             wcscmp(message, L"Windows 10 or later is required.") == 0) &&
            flags == (MB_OK | MB_ICONINFORMATION);
        return IDOK;
    };
    check(!RequireWindows10(false, notice) && versionNotices == 1 && validVersionNotice,
          "unsupported Windows is rejected with one explicit version notice");
    check(RequireWindows10(IsWindows10OrGreater(), notice) && versionNotices == 1,
          "supported Windows passes the manifested version check without a notice");
    KeyboardRouter test;
    test.Mode(true, false);
    check(!test.Feed(VK_LWIN, true).consume, "Win down forwarded");
    for (DWORD vk : {'E', 'D', 'R', 'A', 'L', 'X', 'I', 'S'})
        check(!test.Feed(vk, true).consume && !test.Feed(vk, false).consume, "other Win shortcuts forwarded");
    auto opened = test.Feed('P', true);
    check(opened.consume && opened.action == KeyAction::Open, "Win+P intercepted");
    test.Feed('P', false);
    check(test.Feed(VK_LWIN, false).action == KeyAction::None, "first chord release keeps menu");
    test.Feed(VK_LWIN, true);
    check(test.Feed('P', true).action == KeyAction::Cycle, "second P cycles");
    test.Feed('P', false);
    check(test.Feed(VK_LWIN, false).action == KeyAction::Commit, "cycled Win release commits");
    test.MenuClosed();
    test.Feed(VK_RWIN, true);
    test.Feed('P', true);
    test.Feed(VK_RWIN, false);
    check(test.Feed('P', false).consume, "P up after Win still consumed");
    test.Mode(false, false);
    test.Feed(VK_LWIN, true);
    check(!test.Feed('P', true).consume, "disabled mode passes Win+P");
    test.Feed('P', false);
    test.Feed(VK_LWIN, false);
    test.Mode(true, false);
    test.Feed(VK_LWIN, true);
    test.Feed(VK_LCONTROL, true);
    check(!test.Feed('P', true).consume, "Win+Ctrl+P passes");
    KeyboardRouter stale;
    stale.Mode(true, false);
    stale.Feed(VK_LWIN, true);
    stale.Mode(false, false);
    stale.Mode(true, false);
    check(!stale.Feed('P', true).consume, "reenabling mode clears stale Win state");
    KeyboardRouter native;
    native.Mode(false, true);
    native.Feed(VK_LWIN, true);
    const auto routed = native.Feed('P', true);
    check(!routed.consume && routed.action == KeyAction::NativeRoute && routed.beginNativeRoute,
          "native mode routes while forwarding Win+P");
    native.Feed('P', false);
    const auto nativeCycle = native.Feed('P', true);
    check(!nativeCycle.consume && nativeCycle.action == KeyAction::NativeRoute && !nativeCycle.beginNativeRoute,
          "native repeated P preserves the original screen after Shell takes focus");
    native.Feed('P', false);
    native.Feed(VK_LWIN, false);
    native.Feed(VK_RWIN, true);
    check(native.Feed('P', true).beginNativeRoute,
          "new native Win chord captures a new target screen");
    native.Feed('P', false);
    native.CancelNativeGesture();
    check(native.Feed('P', true).beginNativeRoute,
          "cancelled native chord does not reuse a stale target screen");
    const auto display = QueryCurrentDisplayState();
    report << "DISPLAY mode=" << display.mode << " paths=" << display.activePathCount << "\n";
    test.Feed('P', false);
    test.Feed(VK_RCONTROL, true);
    test.Feed(VK_LCONTROL, false);
    check(!test.Feed('P', true).consume, "one Ctrl release preserves other Ctrl modifier");
    if (!RegisterWindows(GetModuleHandleW(nullptr)))
        return 3;
    trayWindow = CreateWindowW(kTrayClass, L"Self test", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                               GetModuleHandleW(nullptr), this);
    customMode = true;
    optimizeMode = false;
    dryRun = true;
    router.Mode(true, false);
    auto pump = []() {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    };
    auto key = [](DWORD vk, bool down) {
        KBDLLHOOKSTRUCT event{};
        event.vkCode = vk;
        KeyboardCallback(HC_ACTION, down ? WM_KEYDOWN : WM_KEYUP, reinterpret_cast<LPARAM>(&event));
    };
    key(VK_LWIN, true);
    key('P', true);
    key('P', false);
    key(VK_LWIN, false);
    pump();
    check(menuWindow && IsWindowVisible(menuWindow), "live callback first release keeps visible menu");
    RECT menuClient{}, menuFrame{};
    GetClientRect(menuWindow, &menuClient);
    GetWindowRect(menuWindow, &menuFrame);
    check(menuClient.right == menuFrame.right - menuFrame.left && menuClient.bottom == menuFrame.bottom - menuFrame.top,
          "borderless menu preserves the full content client area");
    check(SendMessageW(menuWindow, WM_NCHITTEST, 0, 0) == HTCLIENT,
          "borderless menu does not expose resize or drag borders");
    const HWND openedMenu = menuWindow;
    BOOL nativeRendering = FALSE;
    check(SUCCEEDED(DwmGetWindowAttribute(menuWindow, DWMWA_NCRENDERING_ENABLED,
                                        &nativeRendering, sizeof(nativeRendering))) && nativeRendering,
          "popup retains native DWM nonclient rendering for its system shadow");
    check(GetWindow(menuWindow, GW_OWNER) == nullptr &&
              (GetWindowLongPtrW(menuWindow, GWL_STYLE) & WS_THICKFRAME) != 0 &&
              (GetWindowLongPtrW(menuWindow, GWL_EXSTYLE) & WS_EX_LAYERED) == 0,
          "popup uses a native frame without a separate shadow window or pixel alpha");
    check(display.mode < 0 || selected == display.mode, "menu initialization matches actual topology");
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));
    check(menus.size() == monitors.size(), "one synchronized menu per active display regardless of topology classification");
    for (HWND window : menus) {
        RECT rect{};
        GetWindowRect(window, &rect);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &mi);
        check(abs((rect.left + rect.right) - (mi.rcWork.left + mi.rcWork.right)) <= 2 &&
                  abs((rect.top + rect.bottom) - (mi.rcWork.top + mi.rcWork.bottom)) <= 2,
              "menu centered in correct monitor work area");
    }
    if (menuWindow) {
        const int before = selected;
        SendMessageW(menuWindow, WM_MOUSEMOVE, 0, MAKELPARAM(120, 180));
        check(before == selected, "stationary initial mouse does not replace topology selection");
        const int actual = currentMode;
        const int hoverTarget = (before + 1) % 4;
        RECT client{};
        GetClientRect(menuWindow, &client);
        hoverEnabled = true;
        SendMessageW(menuWindow, WM_MOUSEMOVE, 0,
                     MAKELPARAM((client.right * (2 * hoverTarget + 1)) / 8,
                                modern_ui::Scale(menuWindow, 80)));
        check(hovered == hoverTarget && selected == before && currentMode == actual,
              "mouse hover preserves keyboard selection and actual display mode");
        SendMessageW(menuWindow, WM_MOUSELEAVE, 0, 0);
        check(hovered == -1 && selected == before && currentMode == actual,
              "mouse leave clears only the hover highlight");
    }
    key(VK_LWIN, true);
    key('P', true);
    key('P', false);
    pump();
    check(selected == (display.mode < 0 ? 1 : (display.mode + 1) % 4),
          "callback repeated P advances selection");
    check(menuWindow && !displaySwitch.Pending(), "cycling P keeps menu open without switching displays");
    key(VK_LWIN, false);
    pump();
    check(!menuWindow, "cycled release confirms changed selection (dry run)");
    check(!IsWindow(openedMenu), "closing destroys the popup and releases its native frame");
    key(VK_LWIN, true);
    key('P', true);
    key('P', false);
    key(VK_LWIN, false);
    pump();
    if (menuWindow) {
        selected = currentMode;
        CommitSelection(true);
    }
    check(menuWindow != nullptr, "unchanged release keeps menu open");
    CommitSelection(false);
    check(!menuWindow && !displaySwitch.Pending(), "unchanged explicit confirm closes without switch");
    ShowSettings();
    pump();
    check(settingsWindow && IsWindowVisible(settingsWindow), "settings opens");
    check(IsShellSurface(GetShellWindow() ? GetShellWindow() : GetDesktopWindow()),
          "desktop foreground uses the pointer screen instead of its spanning window bounds");
    check(!IsShellSurface(settingsWindow), "ordinary application windows keep foreground screen priority");
    check(!GetDlgItem(settingsWindow, 105), "settings has no menu preview button");
    check(GetDlgItem(settingsWindow, 106) && showTrayIcon,
          "tray visibility checkbox exists and defaults to checked");
    ActivateSetting(106);
    check(!showTrayIcon && !trayIconAdded && !trayIcon,
          "unchecking tray visibility hides the icon without closing the application");
    SendMessageW(trayWindow, WM_THEMECHANGED, 0, 0);
    SendMessageW(trayWindow, WM_SETTINGCHANGE, 0, 0);
    SendMessageW(trayWindow, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0);
    check(!showTrayIcon && !trayIconAdded && !trayIcon,
          "theme changes and Explorer restart do not restore a disabled tray icon");
    ShowWindow(settingsWindow, SW_HIDE);
    SendMessageW(trayWindow, kShowSettings, 0, 0);
    check(IsWindowVisible(settingsWindow),
          "existing-instance activation opens settings while the tray icon is hidden");
    SetFocus(GetDlgItem(settingsWindow, 106));
    SendMessageW(GetDlgItem(settingsWindow, 106), WM_KEYDOWN, VK_SPACE, 0);
    SendMessageW(GetDlgItem(settingsWindow, 106), WM_KEYUP, VK_SPACE, 0);
    check(showTrayIcon, "tray visibility checkbox supports keyboard activation");
    ActivateSetting(106);
    wchar_t caption[256]{};
    GetWindowTextW(settingsWindow, caption, ARRAYSIZE(caption));
    check(std::wstring(caption).find(kAppVersion) != std::wstring::npos,
          "settings caption contains the application version");
    check(std::wstring(caption).find(winpplus::kName) != std::wstring::npos,
          "settings caption uses the WinPPlus product identity");
    SetSettingsHover(0);
    UpdateWindow(settingsWindow);
    SendMessageW(GetDlgItem(settingsWindow, 101), WM_MOUSEMOVE, 0, MAKELPARAM(20, 20));
    RECT dirty{};
    check(GetUpdateRect(settingsWindow, &dirty, FALSE) &&
              dirty.top >= modern_ui::Scale(settingsWindow, 150) &&
              dirty.bottom <= modern_ui::Scale(settingsWindow, 214),
          "settings hover invalidates only the affected row");
    UpdateWindow(settingsWindow);
    SendMessageW(GetDlgItem(settingsWindow, 101), WM_MOUSEMOVE, 0, MAKELPARAM(20, 20));
    check(!GetUpdateRect(settingsWindow, &dirty, FALSE), "same hover target does not request another repaint");
    SetSettingsHover(-1);
    UpdateWindow(settingsWindow);
    SendMessageW(GetDlgItem(settingsWindow, 103), WM_MOUSEMOVE, 0, MAKELPARAM(20, 20));
    check(settingsHover == -1 && !GetUpdateRect(settingsWindow, &dirty, FALSE),
          "startup checkbox hover does not change background or request repaint");
    SetFocus(GetDlgItem(settingsWindow, 101));
    SendMessageW(GetDlgItem(settingsWindow, 101), WM_KEYDOWN, VK_SPACE, 0);
    SendMessageW(GetDlgItem(settingsWindow, 101), WM_KEYUP, VK_SPACE, 0);
    check(optimizeMode && !customMode, "native mode disables custom mode");
    ActivateSetting(102);
    check(customMode && !optimizeMode, "custom mode disables native mode");
    ActivateSetting(102);
    check(!customMode && !optimizeMode, "clicking the active mode restores Windows defaults");
    ActivateSetting(100);
    check(!customMode && !optimizeMode, "default menu disables both custom modes");
    ShowMenu(true);
    pump();
    check(menuWindow && menuPreviewSession && !customMode && !optimizeMode,
          "visual preview opens without changing configured modes");
    selected = (currentMode + 1) % 4;
    CommitSelection(false);
    check(!menuWindow && !displaySwitch.Pending(), "visual preview never starts a display switch");
    ShowMenu(true);
    pump();
    const int initialSelection = selected;
    SendMessageW(menuWindow, WM_KEYDOWN, VK_RIGHT, 0);
    check(selected == (initialSelection + 1) % 4 && currentMode == display.mode,
          "arrow changes pending selection independently of current mode");
    RECT menuRect{};
    GetClientRect(menuWindow, &menuRect);
    SendMessageW(
        menuWindow, WM_LBUTTONUP, 0,
        MAKELPARAM(menuRect.right - modern_ui::Scale(menuWindow, 27), modern_ui::Scale(menuWindow, 25)));
    check(!menuWindow && !displaySwitch.Pending(), "header close button dismisses without display switching");
    ShowWindow(settingsWindow, SW_HIDE);
    ShowMenu(true);
    pump();
    const auto settingsMenus = menus;
    selected = (currentMode + 1) % 4;
    const int pendingBeforeSettings = selected;
    GetClientRect(menuWindow, &menuRect);
    const LPARAM settingsPoint = MAKELPARAM(menuRect.right - modern_ui::Scale(menuWindow, 65),
                                           modern_ui::Scale(menuWindow, 25));
    hoverEnabled = true;
    SendMessageW(menuWindow, WM_MOUSEMOVE, 0, settingsPoint);
    check(hovered == 5 && selected == pendingBeforeSettings &&
              !modern_ui::MenuCloseHitTest(menuWindow,
                  {GET_X_LPARAM(settingsPoint), GET_Y_LPARAM(settingsPoint)}),
          "settings gear has its own hover target without changing the pending display selection");
    SendMessageW(menuWindow, WM_LBUTTONUP, 0, settingsPoint);
    check(!menuWindow && menus.empty() && IsWindowVisible(settingsWindow) && !displaySwitch.Pending() &&
              std::none_of(settingsMenus.begin(), settingsMenus.end(), [](HWND window) { return IsWindow(window); }),
          "settings gear closes every popup and opens settings without applying a pending display mode");
    ShowMenu(true);
    pump();
    const int clickTarget = (selected + 1) % 4;
    GetClientRect(menuWindow, &menuRect);
    const LPARAM clickPoint = MAKELPARAM((menuRect.right * (2 * clickTarget + 1)) / 8,
                                        modern_ui::Scale(menuWindow, 80));
    hoverEnabled = true;
    SendMessageW(menuWindow, WM_MOUSEMOVE, 0, clickPoint);
    SendMessageW(menuWindow, WM_LBUTTONUP, 0, clickPoint);
    check(!menuWindow && selected == clickTarget && !displaySwitch.Pending(),
          "mouse click confirms its own item independently of keyboard selection");
    check(displaySwitch.Start(-1) == SwitchStart::Failed && displaySwitch.Start(4) == SwitchStart::Failed &&
              !displaySwitch.Pending(),
          "invalid display modes cannot queue a configuration change");
    auto switchState = display;
    check(displaySwitch.Poll(switchState) == SwitchUpdate::None, "idle display switch has no pending result");

    // A blocked graphics API must neither block the owner nor allow overlapping
    // requests, including after reporting a timeout or destroying the owner.
    static HANDLE applyEntered{}, applyRelease{}, applyExited{};
    applyEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    applyRelease = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    applyExited = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (applyEntered && applyRelease && applyExited) {
        auto blockedApply = +[](UINT32, DISPLAYCONFIG_PATH_INFO*, UINT32, DISPLAYCONFIG_MODE_INFO*, UINT32) -> LONG {
            const HANDLE entered = applyEntered, release = applyRelease, exited = applyExited;
            SetEvent(entered);
            WaitForSingleObject(release, 2000);
            SetEvent(exited);
            return ERROR_SUCCESS;
        };
        auto fakeQuery = +[]() { DisplayState value; value.mode = 0; return value; };
        {
            DisplaySwitch blocked(blockedApply, fakeQuery, 50);
            const auto before = GetTickCount64();
            check(blocked.Start(0) == SwitchStart::Started && GetTickCount64() - before < 500,
                  "display request returns while graphics API is blocked");
            check(WaitForSingleObject(applyEntered, 1000) == WAIT_OBJECT_0,
                  "display worker entered the graphics API");
            Sleep(70);
            check(blocked.Poll(switchState) == SwitchUpdate::Unconfirmed && blocked.LastError() == WAIT_TIMEOUT,
                  "blocked graphics API reports timeout without a UI wait");
            check(blocked.Start(1) == SwitchStart::Busy && blocked.Poll(switchState) == SwitchUpdate::None,
                  "timed out worker stays busy and reports timeout only once");
            SetEvent(applyRelease);
            for (int attempt = 0; blocked.Pending() && attempt < 100; ++attempt) {
                blocked.Poll(switchState);
                Sleep(5);
            }
            check(!blocked.Pending(), "finished timed out worker releases its busy state");
        }
        ResetEvent(applyEntered);
        ResetEvent(applyRelease);
        ResetEvent(applyExited);
        auto owner = std::make_unique<DisplaySwitch>(blockedApply, fakeQuery);
        owner->Start(0);
        check(WaitForSingleObject(applyEntered, 1000) == WAIT_OBJECT_0, "second blocked worker started");
        const auto before = GetTickCount64();
        owner.reset();
        check(GetTickCount64() - before < 500, "display switch destruction never joins a blocked worker");
        SetEvent(applyRelease);
        check(WaitForSingleObject(applyExited, 1000) == WAIT_OBJECT_0, "detached worker exits safely after owner destruction");
        auto deniedApply = +[](UINT32, DISPLAYCONFIG_PATH_INFO*, UINT32, DISPLAYCONFIG_MODE_INFO*, UINT32) -> LONG {
            return ERROR_ACCESS_DENIED;
        };
        DisplaySwitch denied(deniedApply, fakeQuery);
        denied.Start(0);
        SwitchUpdate update = SwitchUpdate::None;
        for (int attempt = 0; update == SwitchUpdate::None && attempt < 100; ++attempt) {
            update = denied.Poll(switchState);
            Sleep(5);
        }
        check(update == SwitchUpdate::Failed && denied.LastError() == ERROR_ACCESS_DENIED && !denied.Pending(),
              "graphics API failure preserves the Windows error and releases busy state");
    } else
        check(false, "display worker test events created");
    if (applyEntered) CloseHandle(applyEntered);
    if (applyRelease) CloseHandle(applyRelease);
    if (applyExited) CloseHandle(applyExited);
    const auto scratch = storage.Root() / (L"self-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                           std::to_wstring(GetTickCount64()));
    std::error_code error;
    const bool created = std::filesystem::create_directory(scratch, error);
    check(created, "isolated configuration test directory created");
    if (created) {
        AppStorage isolated(scratch);
        const auto defaults = isolated.Load();
        check(defaults.chinese && defaults.custom && !defaults.native && defaults.showTrayIcon,
              "missing configuration has safe defaults");
        check(isolated.Save({false, false, true, false}), "configuration save succeeds");
        const auto restored = isolated.Load();
        check(!restored.chinese && !restored.custom && restored.native && !restored.showTrayIcon,
              "language, menu mode and hidden tray preference survive round trip");
        const auto configPath = (scratch / L"config.ini").wstring();
        check(WritePrivateProfileStringW(L"General", L"ShowTrayIcon", nullptr, configPath.c_str()) &&
                  isolated.Load().showTrayIcon,
              "older configuration without a tray preference defaults to visible");
        check(isolated.Save({true, true, true}), "conflicting mode fixture saved");
        const auto normalized = isolated.Load();
        check(normalized.custom && !normalized.native,
              "configuration load normalizes mutually exclusive modes");
        // Only remove the unique directory this test successfully created.
        std::filesystem::remove_all(scratch, error);
        check(!error, "isolated configuration test data removed");
    }
    SendMessageW(settingsWindow, WM_CLOSE, 0, 0);
    check(!IsWindowVisible(settingsWindow) && IsWindow(trayWindow),
          "settings close hides without quitting tray");
    if (settingsWindow)
        DestroyWindow(settingsWindow);
    settingsWindow = nullptr;
    DestroyWindow(trayWindow);
    trayWindow = nullptr;
    failures += RunStartupTests(report);
    report << "FAILURES " << failures << "\n";
    return failures ? 3 : 0;
}
