#pragma once

#include <windows.h>
#include <string_view>

namespace modern_ui {
constexpr int kSettingsWidth = 490;
constexpr int kSettingsHeight = 386;
constexpr int kMenuWidth = 640;
constexpr int kMenuHeight = 212;

int Scale(HWND hwnd, int dip);
RECT GetSettingsControlRect(HWND hwnd, int controlId);
int SettingsHitTest(HWND hwnd, POINT point);
int MenuHitTest(HWND hwnd, POINT point);
bool MenuCloseHitTest(HWND hwnd, POINT point);
void DrawSettings(HWND hwnd, HDC dc, bool chinese, bool optimize, bool custom,
    bool startup, int hovered, std::wstring_view status, std::wstring_view startupHint = {});
void DrawMenu(HWND hwnd, HDC dc, bool chinese, int selected, int current, int hovered,
    bool preview = false);
// The caller owns the returned icon and releases it with DestroyIcon.
HICON CreateAppIcon(int size = 32, bool light = false);
}
