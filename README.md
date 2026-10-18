# WinPPlus

[简体中文](README.zh-CN.md) · [Download](https://github.com/zackxjxc/winpplus/releases/latest)

[![Build and release](https://github.com/zackxjxc/winpplus/actions/workflows/build-release.yml/badge.svg)](https://github.com/zackxjxc/winpplus/actions/workflows/build-release.yml)
[![Latest release](https://img.shields.io/github/v/release/zackxjxc/winpplus)](https://github.com/zackxjxc/winpplus/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

**Bring the native Win+P menu to your active display, or use a synchronized custom menu on every active display.**

WinPPlus is a lightweight Windows projection-menu utility. Choose between two ways to keep display switching within reach:

| Mode | What it does |
| --- | --- |
| **Follow the active display** | Opens the original Windows projection menu on the display containing your foreground window, keeping the native interface and interaction. |
| **Custom menu on every display** | Replaces the native menu with a centered menu on **every active desktop display**, so you can make the same synchronized selection from whichever display you can see. |

The two modes are mutually exclusive. You can also restore the default Windows behavior in settings.

[<img src="docs/images/settings-en.png" alt="WinPPlus settings" width="493">](docs/images/settings-en.png)

[<img src="docs/images/menu-en.png" alt="Custom projection menu" width="640">](docs/images/menu-en.png)

Screenshots are lossless PNG captures at native display resolution. Click an image to view the original.

## Getting started

1. Download **WinPPlus.exe** or the portable ZIP from [Releases](https://github.com/zackxjxc/winpplus/releases/latest).
2. If you downloaded the ZIP, extract it to create a single **WinPPlus** folder. Run **WinPPlus.exe** inside that folder and choose a mode in settings.
3. Press **Win+P**. Closing settings keeps WinPPlus in the tray; double-click the tray icon to reopen settings, or right-click it to exit.

One portable EXE, no installer, no separate .NET or Windows App SDK runtime. The optional native-routing DLL is embedded in the EXE and extracted automatically when needed.

## Custom menu controls

- Press Win+P once and release: the menu stays open.
- Keep Win held and press P again to cycle options; release Win to confirm a changed selection.
- Use arrow keys and Enter, or hover and click with the mouse; Esc cancels.
- A blue underline marks the actual display mode. Hover and keyboard selection have separate highlights.
- Confirming the current mode closes the menu without applying the same display configuration again.

Supports **PC screen only, Duplicate, Extend, and Second screen only**, English/Chinese labels, per-monitor DPI scaling and a theme-aware tray icon. Other Windows shortcuts, including Win+E, keep their normal behavior.

## Compatibility and settings

- Windows x64. Current native-routing validation is on Windows 11; the custom menu targets Windows 10 version 1607 and later and Windows 11.
- Native routing depends on the Windows ShellHost implementation and can require updates for different Windows builds.
- Menus can appear only on desktop displays currently enabled in Windows. Disabled or disconnected displays cannot host a window; cloned displays may share one desktop surface.
- Settings and logs: `%APPDATA%\WinPPlus`.
- Optional start with Windows uses the current user's Run entry, checks the executable path and Windows disabled state, and verifies changes by reading them back.

## Build and release

Requires CMake 3.25 or later, Visual Studio 2022 or 2026 with the Desktop development with C++ workload, and Windows SDK 10.0.26100.0. Visual Studio 2026 requires CMake 4.2 or later. CMake uses the installed Visual Studio toolchain; no application runtime is added.

```powershell
cmake --preset windows
cmake --build --preset release --parallel
ctest --preset release
./scripts/package.ps1
```

Open the repository folder directly in Visual Studio, or use the supplied CMake presets. Generated build files stay in `out/build/`; executables are in `out/build/bin/Debug/` or `out/build/bin/Release/`. The Hook DLL is built and embedded automatically. The PowerShell build/test/package scripts use the same CMake project and support Windows PowerShell 5.1 and PowerShell 7.

Release artifacts in `dist/` include `WinPPlus.exe`, `WinPPlus-v<version>-windows-x64.zip`, and `SHA256SUMS.txt`. The ZIP includes both README languages and their images in one `WinPPlus` folder.

**To publish a new version:** increase the root [`VERSION`](VERSION), commit, and push to `main`. GitHub Actions builds and tests Debug and Release, then automatically creates tag `v<version>` and publishes the verified files as a GitHub Release. An unchanged version only runs checks. Existing releases are left unchanged; manual workflow dispatch can retry an unpublished version.

Version numbers use `MAJOR.MINOR.PATCH`. The title bar and Windows file-version resources are generated from this one file. See [developer notes](docs/architecture.md) for module responsibilities and testing details.

## License

[MIT](LICENSE). Third-party copyright and license notices are preserved in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
