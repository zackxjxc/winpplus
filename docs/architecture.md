# 开发说明

## 正式构建

`CMakeLists.txt` 定义应用、Hook DLL 和静态 MinHook 三个目标，统一维护 C++20、Unicode、UTF-8、警告等级、SDK 和静态 CRT。`CMakePresets.json` 提供 Windows x64 配置、Debug/Release 构建和 CTest 测试预设；可用 Visual Studio 直接打开文件夹。

Hook 目标先构建，CMake 再生成对应配置的 `hook_resource.rc`，将 DLL 嵌入应用。资源生成规则直接依赖 DLL，DLL 更新时重新生成并编译资源。`tray_resources.rc` 只维护图标及版本资源，图标路径由 CMake 生成，和构建工作目录无关。`scripts/package.ps1` 构建并测试 Release，使用 `cmake --install` 将分发文件安装到临时目录，再生成单 EXE、便携 ZIP 和 SHA-256 校验文件。ZIP 最外层只有 `WinPPlus/` 文件夹，包含程序、中英文 README、项目许可和第三方许可全文。脚本在打包后校验目录结构。运行时不依赖 WinUI 或 .NET。链接器仅在程序中记录 PDB 文件名，不嵌入本地构建目录。

根目录 `VERSION` 是软件版本的唯一来源。CMake 校验版本后通过 `configure_file` 在构建目录生成版本头文件及 DPI/权限清单，并在 VERSION 变化时自动重新配置；内容不变时不重写生成文件。构建中间文件和程序分别位于 `out/build/`、`out/build/bin/<配置>/`，默认使用已安装的 Visual Studio 工具集和 Windows SDK 10.0.26100.0。CMake 最低版本为 3.25，VS 2026 生成器需要 4.2。

## 模块职责

| 文件 | 职责 |
| --- | --- |
| `src/main.cpp` | 解析启动参数并运行应用 |
| `src/app_identity.h` | 产品中英文名称、数据目录、窗口类名与单实例标识 |
| `src/app_version.h` | 包装由 VERSION 生成的版本定义，供标题栏与 EXE 资源共用 |
| `src/application.h/.cpp` | 应用生命周期、窗口回调桥接、单实例、键盘钩子与定时器 |
| `src/tray_window.cpp` | 托盘图标、深浅主题、托盘菜单和消息分发 |
| `src/settings_window.cpp` | 设置窗口、语言选择、功能互斥和控件交互 |
| `src/projection_menu.cpp` | 多屏菜单窗口、同步选择、鼠标与键盘、无操作确认和预览保护 |
| `src/modern_ui.h/.cpp` | GDI 绘制、DPI 尺寸和点击区域，不执行显示切换 |
| `src/keyboard_router.h` | Win+P 状态机和其他快捷键透传规则 |
| `src/app_storage.h/.cpp` | 配置、日志与嵌入 DLL 释放 |
| `src/startup_registration.h/.cpp` | 当前用户 Run 登记、路径和参数检查、Windows 禁用状态与读回核验 |
| `src/display_state.h/.cpp` | 查询实际 CCD 拓扑，区分内部、外部、复制、扩展及混合布局 |
| `src/display_switch.h/.cpp` | 异步启动 DisplaySwitch、轮询结果和进程句柄释放 |
| `src/native_route_state.h` | 应用与 DLL 共享的 32 字节路由协议 |
| `src/native_hook_controller.h/.cpp` | 同会话 ShellHost 检测、注入、就绪查询和重连 |
| `src/shell_hook.cpp` | ShellHost 内部的 SetWindowPos 钩子 |
| `tests/app_self_test.cpp` | `--self-test` 的回归检查，独立于正常运行流程 |
| `tests/startup_registration_test.cpp` | 隔离注册表数据下的启动状态检查 |

第三方 MinHook 保留上游目录和许可，不作为应用代码进行格式化或裁剪。`.clang-format` 供新增或明确重构的自有代码使用，不要求一次性格式化整个仓库。

## 生命周期和副作用

`Application` 在主线程上拥有所有窗口与键盘钩子。窗口通过 `GWLP_USERDATA` 找到所属实例。低级键盘钩子没有用户数据参数，因此只保留一个静态实例桥接指针。退出时先禁用原生路由，关闭菜单，移除键盘钩子，再释放窗口、托盘图标、注入控制器和单实例句柄。自定义菜单的开始菜单掩码使用带内部标记的 Ctrl 按下/抬起，不向前台应用发送 F24 功能键。

设置页的原生按钮只负责输入、焦点和可访问性，像素统一由父窗口缓冲绘制。悬浮变化只让旧、新控件区域失效，不更新窗口标题或控件文字。开机启动控件不绘制悬浮或按下背景。设置页移除了预览按钮和焦点虚线框，版本号显示在标题栏；Tab 和空格操作继续保留。投屏菜单的实际模式、键盘待选项、鼠标悬浮状态分别保存；实际模式仅使用底部蓝条，键盘待选使用蓝底，悬浮使用灰底。图标与文字作为一组居中，悬浮不改变待选项，点击按命中项确认。

`DisplaySwitch` 独占显示切换进程的句柄；析构只关闭句柄，不终止系统显示切换进程。UI 定时器不等待进程完成，只查询实际状态并确认结果。

自定义菜单按 `EnumDisplayMonitors` 返回的全部活动桌面屏幕创建窗口，不以 CCD 拓扑分类作为窗口数量的前提。所有菜单共享实际模式、待选项和悬浮状态；活动屏幕上的窗口负责初始键盘焦点。断开或被 Windows 禁用的屏幕无法创建菜单，复制模式可能只暴露一个桌面表面。

`AppStorage::Save` 只保存语言与菜单行为。开机启动由 `StartupRegistration` 独立处理。自测不会保存用户设置或修改真实开机启动；启动检查使用 `HKCU/Software/WinPPlus/SelfTests` 下的唯一隔离分支，清除测试值后尝试删除空键，权限不足时在报告中说明。配置测试使用唯一临时子目录。

内部自测使用安全预览查询真实显示状态并运行同一套绘制和交互，确认任何目标都不会调用 DisplaySwitch。该入口不出现在设置界面。正常菜单保留首次松开不关闭、继续按 P 循环、松开 Win 确认、当前模式不重复切换的既有行为。

开机启动使用当前用户 Run 注册项，保存带引号的完整 EXE 路径与 `--background`，写入后读回核验。检查路径存在且指向当前 EXE、参数正确、Windows 未禁用，并检查 `DisableCurrentUserRun` 策略。设置窗口打开时及可见期间每秒刷新一次；异常不勾选并显示原因，用户点击可重新登记当前路径。Windows 已禁用时引导用户打开系统启动设置，不改写禁用标记。

`StartupApproved/Run` 是 Explorer 的未公开兼容数据，只识别已知 12 字节格式和启用/禁用标志，未知格式显示无法确认，不显示已启用。这里保持轻量检查，不实现任务计划程序、服务或自动修复守护。Run 在用户登录后执行，系统可延迟；检查通过不等于已经完成下次登录实测，也不能预知安全软件拦截或未来文件变动。标准 Run 的命令长度限制见 [Microsoft 文档](https://learn.microsoft.com/windows/win32/setupapi/run-and-runonce-registry-keys)。

内部钩子的共享布局、有效期和定位算法未在这次重构中更改。注入采用异步轮询；启用失败、超时及 ShellHost 重启仍由控制器处理。DLL 加载后留在 ShellHost 中，退出应用通过禁用共享路由让钩子透传，不在退出期间强行卸载 DLL。

## 验证与实际范围

Debug 和 Release 均使用 `/W4 /WX` 构建。自测覆盖其他 Win 快捷键放行、Win+P 首次与重复触发、修饰键、原生模式透传、真实窗口创建与居中、初始鼠标保护、无操作确认、设置模式互斥、预览保护、关闭按钮、非法显示模式以及配置读写。

自测直接调用实际键盘回调，向自有窗口发送消息，不全局模拟 Win 快捷键，不实际切换显示模式。CTest 注册应用自测和版本发布规则检查；应用自测通过 PowerShell 脚本创建子进程，避免测试运行器隐藏窗口而干扰原生窗口的可见性断言。脚本支持 CMake 传入目标程序路径，因此自定义构建目录也可运行 `ctest --test-dir <目录> -C <配置>`。

多屏创建数量检查依据当前活动屏幕运行，不能以单屏环境的通过替代双屏硬件验证。四种实际显示切换和 ShellHost 重启后的真实路由仍需要对应硬件及系统环境。原生优化依赖 Windows 11 Shell 的窗口结构，不承诺所有 Windows 10/11 版本均可用。

## 自动发布

主分支推送及 PR 都构建和测试 Debug、Release。只有新历史的根提交、首次出现 VERSION、版本号严格提高，或手动重试时才进入发布阶段。同一历史中的版本降低会失败。构建阶段只有仓库读取权限；发布阶段独立获得 `contents: write`，从本次成功构建的 artifact 中获取发布文件并创建版本标签和 Release。已发布版本不覆盖。

`test-release.ps1` 覆盖首次发布、相同版本跳过、版本提高、手动重试、降级拒绝及非法版本格式。Actions 使用官方 action 的固定提交 SHA。发布包只包含分发文件，不上传 obj、日志、测试配置、临时设计稿或本地历史备份。

## 产品标识

WinPPlus 使用统一的窗口类名、互斥体、共享路由映射和 Hook 导出符号。配置只读写 `%APPDATA%/WinPPlus`，不携带原型的名称或配置迁移代码。

正式资源包含选定的 `assets/app-icon.svg`、两套主题 ICO 和可重现图标的 GDI+ 开发脚本。文档截图为实际窗口的原始像素 PNG；`--preview-menu --dry-run` 供截图检查使用，焦点转移时保持预览窗口显示，正常 Win+P 菜单的焦点关闭行为不变。
