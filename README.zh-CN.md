# 投屏菜单增强 · WinPPlus

[English](README.md) · [下载](https://github.com/zackxjxc/winpplus/releases/latest)

[![Build and release](https://github.com/zackxjxc/winpplus/actions/workflows/build-release.yml/badge.svg)](https://github.com/zackxjxc/winpplus/actions/workflows/build-release.yml)
[![Latest release](https://img.shields.io/github/v/release/zackxjxc/winpplus)](https://github.com/zackxjxc/winpplus/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

**让原生 Win+P 菜单跟随活动屏幕，或在每个活动屏幕上使用同步的自定义投屏菜单。**

WinPPlus（投屏菜单增强）是一款轻量的 Windows 投屏菜单工具，提供两种方式，让你在看得见的屏幕上完成显示模式切换：

| 模式 | 核心作用 |
| --- | --- |
| **原生菜单跟随活动屏幕** | 将 Windows 原有的 Win+P 菜单显示在当前前台窗口所在的屏幕，保留系统界面及交互。 |
| **每个屏幕都有自定义菜单** | 替换原生菜单，在 **Windows 当前启用的每个桌面屏幕中央同时显示** 自定义菜单。各屏选择同步，在能看见的任意屏幕上都可以操作。 |

两种增强模式互斥，也可以在设置中恢复 Windows 默认行为。

[<img src="docs/images/settings-zh.png" alt="投屏菜单增强设置界面" width="493">](docs/images/settings-zh.png)

[<img src="docs/images/menu-zh.png" alt="自定义投屏菜单" width="640">](docs/images/menu-zh.png)

截图使用屏幕原始分辨率的无损 PNG，点击图片可查看原图。

## 开始使用

1. 在 [Releases](https://github.com/zackxjxc/winpplus/releases/latest) 下载 **WinPPlus.exe** 或便携 ZIP。
2. 如果下载的是 ZIP，解压后会得到一个 **WinPPlus** 文件夹。打开其中的 **WinPPlus.exe**，在设置中选择一种模式。
3. 按下 **Win+P**。关闭设置窗口后软件驻留托盘；双击托盘图标重新打开设置，右键菜单可以退出。

单个便携 EXE，无需安装器，无需额外安装 .NET 或 Windows App SDK 运行时。原生菜单优化所用 DLL 内嵌在 EXE 中，启用时自动释放。

## 自定义菜单操作

- 第一次按下 Win+P 后松开，菜单保持显示。
- 按住 Win，继续按 P 循环选择；松开 Win，确认已改变的选择。
- 支持方向键与 Enter，以及鼠标悬浮和点击；Esc 取消。
- 蓝色底线标记实际显示模式，鼠标悬浮和键盘待选项分别高亮。
- 确认当前模式只关闭菜单，不重复执行显示切换。

支持 **仅电脑屏幕、复制、扩展、仅第二屏幕** 四种模式，中英文切换、每屏 DPI 缩放和随系统主题切换的托盘图标。Win+E 等其他 Windows 快捷键保持原有行为。

## 兼容与配置

- 面向 Windows x64。目前原生菜单路由的实机验证环境为 Windows 11；自定义菜单兼容目标为 Windows 10 1607 及以上和 Windows 11。
- 原生菜单优化依赖 Windows ShellHost 的实现，不同 Windows 版本可能需要适配。
- 只能在 Windows 当前启用的桌面屏幕上创建菜单。已禁用或断开的屏幕无法显示窗口；复制模式中的屏幕可能共用一个桌面表面。
- 配置及日志位于 `%APPDATA%\WinPPlus`。
- 可选随 Windows 登录启动，使用当前用户 Run 注册项，检查启动文件路径及 Windows 禁用状态，并在修改后读回确认。

## 构建与自动发布

需要 CMake 3.25 及以上、Visual Studio 2022 或 2026 的“使用 C++ 的桌面开发”工作负载，以及 Windows SDK 10.0.26100.0。使用 Visual Studio 2026 时需要 CMake 4.2 及以上。CMake 使用本机安装的 Visual Studio 工具链，不增加应用运行时依赖。

```powershell
cmake --preset windows
cmake --build --preset release --parallel
ctest --preset release
./scripts/package.ps1
```

可直接用 Visual Studio 打开仓库文件夹，也可以使用提供的 CMake 预设。构建中间文件位于 `out/build/`，程序位于 `out/build/bin/Debug/` 或 `out/build/bin/Release/`。Hook DLL 会自动先行构建并内嵌。PowerShell 构建、测试和打包脚本也使用同一套 CMake 工程，支持 Windows PowerShell 5.1 和 PowerShell 7。

`dist/` 中生成 EXE、带版本号的便携 ZIP 和 SHA-256 校验文件。ZIP 将中英文 README 及截图一起放在单个 `WinPPlus` 文件夹中。

**发布新版本：** 提高根目录 [`VERSION`](VERSION) 中的版本号，提交并推送到 `main`。GitHub Actions 会构建并测试 Debug、Release，通过后自动创建 `v<版本号>` 标签和 GitHub Release，并上传经过验证的发布文件。普通代码提交只运行检查，不重复发版。已发布的版本不会被覆盖；尚未发布成功的版本可通过手动运行工作流重试。

版本号使用 `主版本.次版本.修订版本` 格式，标题栏和 Windows 文件版本信息均从这一文件生成。模块划分和测试范围见 [开发说明](docs/architecture.md)。

## 许可

本项目采用 [MIT 许可证](LICENSE)，第三方版权及许可保留在 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 中。
