#include "application.h"
#include "modern_ui.h"
#include "app_version.h"

#include <algorithm>
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
    check(!routed.consume && routed.action == KeyAction::NativeRoute,
          "native mode routes while forwarding Win+P");
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
    check(!GetDlgItem(settingsWindow, 105), "settings has no menu preview button");
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
          "invalid display modes cannot launch a process");
    check(displaySwitch.Poll(display) == SwitchUpdate::None, "idle display switch has no pending result");
    const auto scratch = storage.Root() / (L"self-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                           std::to_wstring(GetTickCount64()));
    std::error_code error;
    const bool created = std::filesystem::create_directory(scratch, error);
    check(created, "isolated configuration test directory created");
    if (created) {
        AppStorage isolated(scratch);
        const auto defaults = isolated.Load();
        check(defaults.chinese && defaults.custom && !defaults.native,
              "missing configuration has safe defaults");
        check(isolated.Save({false, false, true}), "configuration save succeeds");
        const auto restored = isolated.Load();
        check(!restored.chinese && !restored.custom && restored.native,
              "language and menu mode survive round trip");
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
