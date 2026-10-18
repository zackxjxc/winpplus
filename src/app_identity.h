#pragma once

#define WINPPLUS_PRODUCT_NAME "WinPPlus"
#define WINPPLUS_FILE_DESCRIPTION "WinPPlus - Windows projection menu enhancements"

#ifndef RC_INVOKED
namespace winpplus {
inline constexpr wchar_t kName[] = L"WinPPlus";
inline constexpr wchar_t kSettingsTitleZh[] = L"投屏菜单增强 · WinPPlus";
inline constexpr wchar_t kSettingsTitleEn[] = L"WinPPlus Settings";
inline constexpr wchar_t kTrayTip[] = L"WinPPlus · 投屏菜单增强";
inline constexpr wchar_t kDataFolder[] = L"WinPPlus";
inline constexpr wchar_t kSettingsClass[] = L"WinPPlus.Settings";
inline constexpr wchar_t kMenuClass[] = L"WinPPlus.CustomMenu";
inline constexpr wchar_t kTrayClass[] = L"WinPPlus.Tray";
inline constexpr wchar_t kSingleton[] = L"Local\\WinPPlus.Tray.Singleton";
}
#endif
