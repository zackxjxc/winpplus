#pragma once

#include <windows.h>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "app_storage.h"
#include "app_identity.h"
#include "display_switch.h"
#include "keyboard_router.h"
#include "native_hook_controller.h"
#include "startup_registration.h"

struct AppOptions {
    bool background{};
    bool preview{};
    bool dryRun{};
    bool selfTest{};
};

// One instance owns the UI thread, all windows and the keyboard hook. Windows
// carry the owning instance in GWLP_USERDATA; only the global hook needs a bridge.
class Application final {
  public:
    explicit Application(AppOptions options);
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    int Run(HINSTANCE instance);

  private:
    static constexpr auto kSettingsClass = winpplus::kSettingsClass;
    static constexpr auto kMenuClass = winpplus::kMenuClass;
    static constexpr auto kTrayClass = winpplus::kTrayClass;
    static constexpr UINT kTrayEvent = WM_APP + 11, kShowSettings = WM_APP + 10;
    static constexpr UINT kKeyAction = WM_APP + 20, kFocusCheck = WM_APP + 21;
    static constexpr ULONG_PTR kInputTag = 0x57505011;
    static constexpr std::array<int, 5> kSettingsControls{100, 101, 102, 103, 104};
    static Application* keyboardOwner;

    static Application* WindowOwner(HWND hwnd, UINT msg, LPARAM lp);
    static LRESULT CALLBACK SettingsCallback(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK MenuCallback(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK TrayCallback(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK KeyboardCallback(int code, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK SettingButtonCallback(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static UINT DpiForMonitor(HMONITOR monitor);
    static void RoundWindow(HWND hwnd);
    static void MaskStartMenu();

    const wchar_t* Tr(const wchar_t* zh, const wchar_t* en) const;
    bool RegisterWindows(HINSTANCE instance);
    void RepaintMenus();
    void RefreshSettings();
    void RefreshStartupState();
    const wchar_t* StartupHint() const;
    void SetSettingsHover(int id);
    void SetStatus(const std::wstring& text);
    void SaveConfig();
    void LoadConfig();
    HMONITOR ActiveMonitor(bool foregroundFirst);
    void CloseMenu();
    void CommitSelection(bool releasedWin);
    void ShowMenu(bool visualPreview = false);
    void ChangeSelection(int delta);
    void ConfigureModes();
    void ActivateSetting(int id);
    void LayoutControls(HWND hwnd);
    void ShowSettings();
    void AddTrayIcon();
    void Tick();
    int RunSelfTests();
    LRESULT MenuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT KeyboardProc(int code, WPARAM wp, LPARAM lp);
    LRESULT SettingButtonProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT TrayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    AppOptions options_;
    AppStorage storage;
    StartupRegistration startup;
    StartupInfo startupInfo;
    ULONGLONG nextStartupCheck{};
    DisplaySwitch displaySwitch;
    HWND trayWindow{}, settingsWindow{}, menuWindow{};
    HHOOK keyboardHook{};
    HICON appIcon{}, trayIcon{};
    HANDLE instanceMutex{};
    WNDPROC buttonProc{};
    bool trayIconAdded{};
    std::vector<HWND> menus;
    KeyboardRouter router;
    std::unique_ptr<NativeHookController> nativeHook;
    bool chinese = true, customMode = true, optimizeMode = false;
    bool dryRun{}, selfTest{}, previewMenu{}, menuPreviewSession{};
    bool closingMenu{}, openingMenu{}, hoverEnabled{};
    int selected = -1, currentMode = -1, hovered = -1, settingsHover = -1;
    POINT initialCursor{};
    HMONITOR triggerMonitor{};
    std::wstring statusText;
};
