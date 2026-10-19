#include "application.h"
#include "modern_ui.h"

#include <shellapi.h>
#include <shellscalingapi.h>
#include <dwmapi.h>
#include <algorithm>

Application* Application::keyboardOwner{};

Application::Application(AppOptions options)
    : options_(options), dryRun(options.dryRun), selfTest(options.selfTest), previewMenu(options.preview) {
    keyboardOwner = this;
}

Application::~Application() {
    if (nativeHook)
        nativeHook->Enable(false);
    CloseMenu();
    if (keyboardHook)
        UnhookWindowsHookEx(keyboardHook);
    keyboardHook = nullptr;
    if (settingsWindow)
        DestroyWindow(settingsWindow);
    if (trayWindow) {
        KillTimer(trayWindow, 1);
        KillTimer(trayWindow, 2);
        if (trayIconAdded) {
            NOTIFYICONDATAW icon{sizeof(icon)};
            icon.hWnd = trayWindow;
            icon.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &icon);
        }
        DestroyWindow(trayWindow);
    }
    nativeHook.reset();
    if (appIcon)
        DestroyIcon(appIcon);
    if (trayIcon)
        DestroyIcon(trayIcon);
    if (instanceMutex)
        CloseHandle(instanceMutex);
    keyboardOwner = nullptr;
}

Application* Application::WindowOwner(HWND hwnd, UINT msg, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    return reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK Application::SettingsCallback(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* app = WindowOwner(hwnd, msg, lp);
    return app ? app->SettingsProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK Application::MenuCallback(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* app = WindowOwner(hwnd, msg, lp);
    return app ? app->MenuProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK Application::TrayCallback(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* app = WindowOwner(hwnd, msg, lp);
    return app ? app->TrayProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK Application::KeyboardCallback(int code, WPARAM wp, LPARAM lp) {
    return keyboardOwner ? keyboardOwner->KeyboardProc(code, wp, lp) : CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK Application::SettingButtonCallback(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* app = WindowOwner(GetParent(hwnd), 0, 0);
    return app ? app->SettingButtonProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

bool Application::RegisterWindows(HINSTANCE instance) {
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = appIcon;
    wc.lpfnWndProc = SettingsCallback;
    wc.lpszClassName = kSettingsClass;
    if (!RegisterClassW(&wc))
        return false;
    wc.lpfnWndProc = MenuCallback;
    wc.lpszClassName = kMenuClass;
    if (!RegisterClassW(&wc))
        return false;
    wc.lpfnWndProc = TrayCallback;
    wc.lpszClassName = kTrayClass;
    wc.style = 0;
    return RegisterClassW(&wc) != 0;
}

const wchar_t* Application::Tr(const wchar_t* zh, const wchar_t* en) const {
    return chinese ? zh : en;
}

void Application::SetStatus(const std::wstring& text) {
    if (text == statusText)
        return;
    statusText = text;
    RefreshSettings();
}

void Application::SaveConfig() {
    if (!selfTest && !storage.Save({chinese, customMode, optimizeMode}))
        SetStatus(Tr(L"无法保存设置", L"Could not save settings"));
}

void Application::LoadConfig() {
    const auto settings = storage.Load();
    chinese = settings.chinese;
    customMode = settings.custom;
    optimizeMode = settings.native;
    RefreshStartupState();
}

UINT Application::DpiForMonitor(HMONITOR monitor) {
    UINT x = 96, y = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y)))
        x = 96;
    return x;
}

bool Application::IsShellSurface(HWND window) {
    if (!window) return false;
    HWND root = GetAncestor(window, GA_ROOT);
    if (!root) root = window;
    if (root == GetShellWindow() || root == GetDesktopWindow()) return true;
    wchar_t cls[128]{};
    GetClassNameW(root, cls, ARRAYSIZE(cls));
    return wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0 ||
           wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0;
}

HMONITOR Application::ActiveMonitor(bool foregroundFirst) {
    HWND foreground = GetForegroundWindow();
    if (foregroundFirst && foreground && foreground != settingsWindow && foreground != menuWindow &&
        !IsShellSurface(foreground) && !(nativeHook && nativeHook->IsProjectionWindow(foreground)))
        return MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    POINT point{};
    if (GetCursorPos(&point))
        return MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    return MonitorFromWindow(foreground, MONITOR_DEFAULTTOPRIMARY);
}

void Application::RoundWindow(HWND hwnd) {
    const DWORD round = 2;
    DwmSetWindowAttribute(hwnd, 33, &round, sizeof(round));
    const BOOL dark = FALSE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
}

void Application::MaskStartMenu() {
    INPUT input[2]{};
    for (auto& event : input) {
        event.type = INPUT_KEYBOARD;
        // A modifier-only pair suppresses the bare-Win Start action without
        // delivering an unhandled function key (and possible alert) to the app.
        event.ki.wVk = VK_LCONTROL;
        event.ki.dwExtraInfo = kInputTag;
    }
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, input, sizeof(INPUT));
}

LRESULT Application::KeyboardProc(int code, WPARAM wp, LPARAM lp) {
    if (code < 0)
        return CallNextHookEx(keyboardHook, code, wp, lp);
    const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
    if (key->dwExtraInfo == kInputTag)
        return CallNextHookEx(keyboardHook, code, wp, lp);
    const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN, up = wp == WM_KEYUP || wp == WM_SYSKEYUP;
    if (!down && !up)
        return CallNextHookEx(keyboardHook, code, wp, lp);
    const auto decision = router.Feed(key->vkCode, down);
    if (optimizeMode && nativeHook && down &&
        ((router.WinHeld() && key->vkCode != 'P' && key->vkCode != VK_LWIN && key->vkCode != VK_RWIN) ||
         key->vkCode == VK_ESCAPE)) {
        nativeHook->RouteTo(nullptr);
        router.CancelNativeGesture();
        nativeTriggerMonitor = nullptr;
        nativeFastUntil = 0;
        KillTimer(trayWindow, 2);
    }
    if (decision.action == KeyAction::NativeRoute && nativeHook) {
        if (decision.beginNativeRoute || !nativeTriggerMonitor)
            nativeTriggerMonitor = ActiveMonitor(true);
        nativeHook->RouteTo(nativeTriggerMonitor);
        // Only the opening gesture needs a short, faster poll. Keep the normal
        // background timer at 250 ms when no native menu is being opened.
        nativeFastUntil = GetTickCount64() + 2000;
        SetTimer(trayWindow, 2, 16, nullptr);
    }
    if (up && !router.WinHeld())
        nativeTriggerMonitor = nullptr;
    if (decision.action == KeyAction::Open)
        triggerMonitor = ActiveMonitor(false);
    if (decision.maskStart && !selfTest)
        MaskStartMenu();
    if (decision.action != KeyAction::None && decision.action != KeyAction::NativeRoute)
        PostMessageW(trayWindow, kKeyAction, static_cast<WPARAM>(decision.action), 0);
    return decision.consume ? 1 : CallNextHookEx(keyboardHook, code, wp, lp);
}

void Application::ConfigureModes() {
    CloseMenu();
    nativeTriggerMonitor = nullptr;
    nativeFastUntil = 0;
    KillTimer(trayWindow, 2);
    router.Mode(customMode, optimizeMode);
    if (!selfTest)
        router.Synchronize();
    if (nativeHook)
        nativeHook->Enable(optimizeMode);
    if (!selfTest && (customMode || optimizeMode) && !keyboardHook)
        keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardCallback, GetModuleHandleW(nullptr), 0);
    if (!customMode && !optimizeMode && keyboardHook) {
        UnhookWindowsHookEx(keyboardHook);
        keyboardHook = nullptr;
    }
    if (!selfTest && (customMode || optimizeMode) && !keyboardHook) {
        customMode = false;
        optimizeMode = false;
        if (nativeHook)
            nativeHook->Enable(false);
        SetStatus(Tr(L"快捷键监听启动失败", L"Keyboard hook could not be started"));
    } else
        SetStatus(customMode     ? Tr(L"自定义菜单已启用", L"Custom menu enabled")
                  : optimizeMode ? L""
                                 : Tr(L"使用 Windows 默认菜单", L"Using the default Windows menu"));
    RefreshSettings();
}

void Application::Tick() {
    if (!selfTest && settingsWindow && IsWindowVisible(settingsWindow) && GetTickCount64() >= nextStartupCheck)
        RefreshStartupState();
    if (nativeHook && optimizeMode) {
        nativeHook->Tick();
        SetStatus(nativeHook->Ready()       ? Tr(L"系统菜单优化已启用", L"System menu optimization enabled")
                  : nativeHook->LastError() ? Tr(L"系统菜单连接失败", L"System menu connection failed")
                                            : L"");
    }
    if (!displaySwitch.Pending())
        return;
    DisplayState state;
    switch (displaySwitch.Poll(state)) {
    case SwitchUpdate::Confirmed:
        SetStatus(Tr(L"显示配置已更新", L"Display configuration updated"));
        storage.Log(L"SWITCH_CONFIRMED " + state.summary);
        break;
    case SwitchUpdate::Unconfirmed:
        SetStatus(
            Tr(L"未确认显示切换，请检查显示器连接", L"Switch not confirmed; check display connections"));
        storage.Log(L"SWITCH_UNCONFIRMED " + state.summary);
        break;
    case SwitchUpdate::Failed:
        SetStatus(Tr(L"无法应用显示配置", L"Could not apply display configuration"));
        storage.Log(L"SWITCH_FAILED error=" + std::to_wstring(displaySwitch.LastError()));
        break;
    case SwitchUpdate::None:
        break;
    }
}

int Application::Run(HINSTANCE instance) {
    if (selfTest) {
        std::error_code error;
        std::filesystem::create_directories(storage.Root(), error);
        return RunSelfTests();
    }
    instanceMutex = CreateMutexW(nullptr, TRUE, winpplus::kSingleton);
    if (!instanceMutex)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND previous = FindWindowW(kTrayClass, nullptr))
            PostMessageW(previous, kShowSettings, 0, 0);
        return 0;
    }
    LoadConfig();
    appIcon = modern_ui::CreateAppIcon(32);
    if (!RegisterWindows(instance))
        return 1;
    trayWindow = CreateWindowExW(WS_EX_TOOLWINDOW, kTrayClass, winpplus::kName, WS_POPUP, 0, 0, 0, 0,
                                 nullptr, nullptr, instance, this);
    if (!trayWindow)
        return 1;
    nativeHook = std::make_unique<NativeHookController>(storage.ExtractHook());
    AddTrayIcon();
    ConfigureModes();
    SetTimer(trayWindow, 1, 250, nullptr);
    if (!options_.background && !options_.preview)
        ShowSettings();
    if (options_.preview) {
        customMode = true;
        optimizeMode = false;
        ConfigureModes();
        triggerMonitor = ActiveMonitor(false);
        ShowMenu();
    }
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (settingsWindow && IsWindowVisible(settingsWindow) &&
            (msg.hwnd == settingsWindow || IsChild(settingsWindow, msg.hwnd)) && msg.message == WM_KEYDOWN &&
            msg.wParam == VK_TAB) {
            const auto& ids = kSettingsControls;
            const int count = static_cast<int>(ids.size());
            const auto found = std::find(ids.begin(), ids.end(), GetDlgCtrlID(GetFocus()));
            const bool reverse = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            int index = found == ids.end()
                            ? (reverse ? count - 1 : 0)
                            : (static_cast<int>(found - ids.begin()) + (reverse ? count - 1 : 1)) % count;
            SetFocus(GetDlgItem(settingsWindow, ids[static_cast<size_t>(index)]));
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
