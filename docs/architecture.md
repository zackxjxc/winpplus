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
| `src/display_switch.h/.cpp` | 后台调用 SetDisplayConfig、确认拓扑及非阻塞轮询 |
| `src/native_route_state.h` | 应用与 DLL 共享的 32 字节路由协议 |
| `src/native_hook_controller.h/.cpp` | 同会话 ShellHost 注入，以及 Win 10 投影侧边栏的外部路由 |
| `src/shell_hook.cpp` | ShellHost 内部的 SetWindowPos 钩子 |
| `tests/app_self_test.cpp` | `--self-test` 的回归检查，独立于正常运行流程 |
| `tests/startup_registration_test.cpp` | 隔离注册表数据下的启动状态检查 |

第三方 MinHook 保留上游目录和许可，不作为应用代码进行格式化或裁剪。`.clang-format` 供新增或明确重构的自有代码使用，不要求一次性格式化整个仓库。

## 生命周期和副作用

`Application` 在主线程上拥有所有窗口与键盘钩子。窗口通过 `GWLP_USERDATA` 找到所属实例。低级键盘钩子没有用户数据参数，因此只保留一个静态实例桥接指针。退出时先禁用原生路由，关闭菜单，移除键盘钩子，再释放窗口、托盘图标、注入控制器和单实例句柄。自定义菜单的开始菜单掩码使用带内部标记的 Ctrl 按下/抬起，不向前台应用发送 F24 功能键。

设置页的原生按钮只负责输入、焦点和可访问性，像素统一由父窗口缓冲绘制。悬浮变化只让旧、新控件区域失效，不更新窗口标题或控件文字。开机启动控件不绘制悬浮或按下背景。设置页移除了预览按钮和焦点虚线框，版本号显示在标题栏；Tab 和空格操作继续保留。投屏菜单的实际模式、键盘待选项、鼠标悬浮状态分别保存；实际模式仅使用底部蓝条，键盘待选使用蓝底，悬浮使用灰底。图标与文字作为一组居中，悬浮不改变待选项，点击按命中项确认。

`DisplaySwitch` 在后台线程通过 `SetDisplayConfig(SDC_APPLY | SDC_TOPOLOGY_*)` 请求 Windows 保存的拓扑，再在同一后台线程查询实际结果；不启动 DisplaySwitch.exe。UI 定时器只读取已完成的结果，不调用显示驱动查询。松开 Win 时使用打开菜单时的显示快照；外部显示变更通过 WM_DISPLAYCHANGE 关闭旧菜单。切换期间不再打开会查询驱动的菜单。

后台状态由工作线程和拥有者共享，线程不持有应用或窗口指针。12 秒未完成时报告未确认；驱动仍未返回时保持忙状态，避免积累并发切换。析构仅请求取消后续轮询，不等待阻塞的驱动调用；已进入驱动的调用无法强制取消。

自定义菜单按 `EnumDisplayMonitors` 返回的全部活动桌面屏幕创建窗口，不以 CCD 拓扑分类作为窗口数量的前提。所有菜单共享实际模式、待选项和悬浮状态；活动屏幕上的窗口负责初始键盘焦点。断开或被 Windows 禁用的屏幕无法创建菜单，复制模式可能只暴露一个桌面表面。

`AppStorage::Save` 只保存语言与菜单行为。开机启动由 `StartupRegistration` 独立处理。自测不会保存用户设置或修改真实开机启动；启动检查使用 `HKCU/Software/WinPPlus/SelfTests` 下的唯一隔离分支，清除测试值后尝试删除空键，权限不足时在报告中说明。配置测试使用唯一临时子目录。

内部自测使用安全预览查询真实显示状态并运行同一套绘制和交互，确认任何目标都不会调用 DisplaySwitch。该入口不出现在设置界面。正常菜单保留首次松开不关闭、继续按 P 循环、松开 Win 确认、当前模式不重复切换的既有行为。

开机启动使用当前用户 Run 注册项，保存带引号的完整 EXE 路径与 `--background`，写入后读回核验。检查路径存在且指向当前 EXE、参数正确、Windows 未禁用，并检查 `DisableCurrentUserRun` 策略。设置窗口打开时及可见期间每秒刷新一次；异常不勾选并显示原因，用户点击可重新登记当前路径。Windows 已禁用时引导用户打开系统启动设置，不改写禁用标记。

`StartupApproved/Run` 是 Explorer 的未公开兼容数据，只识别已知 12 字节格式和启用/禁用标志，未知格式显示无法确认，不显示已启用。这里保持轻量检查，不实现任务计划程序、服务或自动修复守护。Run 在用户登录后执行，系统可延迟；检查通过不等于已经完成下次登录实测，也不能预知安全软件拦截或未来文件变动。标准 Run 的命令长度限制见 [Microsoft 文档](https://learn.microsoft.com/windows/win32/setupapi/run-and-runonce-registry-keys)。

Win 11 内部钩子的共享布局、有效期和定位算法保持原样。注入采用异步轮询；DLL 加载后留在 ShellHost 中，退出应用通过禁用共享路由让钩子透传，不在退出期间强行卸载 DLL。

Win 10 的侧边栏由 ShellExperienceHost 承载，控制器通过 FindWindowEx 查找同会话进程的 Windows.UI.Core.CoreWindow，并只处理标题为“投影”或“Project”的窗口。EnumWindows 会遗漏该沉浸式窗口。Win+P 发出后允许 5 秒等待窗口出现；找到可见且未被 DWM 隐藏的投影窗口后，在其显示期间持续保持目标位置，直到隐藏、取消或更换目标，避免 Shell 较晚的布局覆盖路由。Win 11 的共享意图有效期保持 1.5 秒。Win 10 使用 UI Automation 读取 DevicesFlowFrame 的实际内容宽度，靠工作区右侧放置；不使用 CoreWindow 外框宽度或目标屏幕 DPI 推算。单屏 VM 已复现外框加宽至 630 像素而内容仍为 450 像素的问题，并验证控制器从这种初始状态恢复正确宽度与位置。可见期间每秒复查内容尺寸，避免 Shell 重新布局后沿用旧值。UIA 查询在独立 COM 工作线程中运行，同时至多一个查询，UI 线程只读取已完成结果；2 秒未返回时报告超时，销毁控制器不等待提供程序。不向 ShellExperienceHost 注入 DLL。

同一次按住 Win 的操作只在首次按 P 时捕获目标屏幕，后续 P 沿用该目标；松开全部 Win 键后可重新捕获。普通应用按前台窗口所在屏幕路由；前台为跨屏桌面（Progman/WorkerW）、任务栏或投影窗口时，改用鼠标所在屏幕，因此点击空白桌面不需要输入框焦点。其他 Win 快捷键会取消原生路由。当前匹配范围为中英文系统标题。

Win 10 每次触发后用 16 ms 临时计时器加快前两秒的定位，随后恢复 250 ms 后台轮询；复用同一 HWND/进程/DPI 下已验证的菜单内容宽度，并尝试在放行 Win+P 前预定位已有窗口。首次打开仍需等 Shell 创建窗口和 UIA 内容出现，Shell 也可能覆盖预定位；这种外部搬移方案不能保证目标屏幕从第一帧起显示、完全没有主屏闪现。Win 11 内部钩子的创建阶段定位保持原样。

自定义菜单使用无系统边框的逐像素透明窗口，白色内容绘制后按圆角覆盖率生成预乘 BGRA，再经 UpdateLayeredWindow 提交；圆角外部真正透明。阴影是位于菜单下方的独立分层 owner 窗口，使用同一圆角轮廓生成柔和扩散，不接收鼠标或焦点。菜单关闭时同步销毁阴影，DPI 或位置变化时同步更新。Win 10 VM 的 125% 桌面截图用于检查圆角和阴影，命中检查验证透明角和阴影区域可穿透。实现参考 [borderless-window](https://github.com/rossy/borderless-window) 与 [LayeredWindow](https://github.com/dhpoware/LayeredWindow) 的 Win32 窗口和 alpha 合成用法。设置页圆环使用 GDI+ 抗锯齿椭圆绘制。

## 验证与实际范围

Debug 和 Release 均使用 `/W4 /WX` 构建。自测覆盖其他 Win 快捷键放行、Win+P 首次与重复触发、修饰键、原生模式透传、真实窗口创建与居中、初始鼠标保护、无操作确认、设置模式互斥、预览保护、关闭按钮、非法显示模式以及配置读写。

自测直接调用实际键盘回调，向自有窗口发送消息，不全局模拟 Win 快捷键，不实际切换显示模式。CTest 注册应用自测和版本发布规则检查；应用自测通过 PowerShell 脚本创建子进程，避免测试运行器隐藏窗口而干扰原生窗口的可见性断言。脚本支持 CMake 传入目标程序路径，因此自定义构建目录也可运行 `ctest --test-dir <目录> -C <配置>`。

多屏创建数量检查依据当前活动屏幕运行，不能以单屏环境的通过替代双屏硬件验证。新增回归检查模拟阻塞的图形 API，确认请求、轮询、超时和销毁不会等待，并验证驱动错误传播及并发请求保护。Win 10 19045 单屏 VM 已验证侧边栏移位后的恢复、实际内部拓扑确认和不支持复制时的错误返回；双屏拓扑切换、跨屏 DPI 与 Shell 重启仍需对应硬件环境。原生优化依赖各版本 Shell 的窗口结构，不承诺所有 Windows 10/11 版本均可用。

## 自动发布

主分支推送及 PR 都构建和测试 Debug、Release。只有新历史的根提交、首次出现 VERSION、版本号严格提高，或手动重试时才进入发布阶段。同一历史中的版本降低会失败。构建阶段只有仓库读取权限；发布阶段独立获得 `contents: write`，从本次成功构建的 artifact 中获取发布文件并创建版本标签和 Release。已发布版本不覆盖。

`test-release.ps1` 覆盖首次发布、相同版本跳过、版本提高、手动重试、降级拒绝及非法版本格式。Actions 使用官方 action 的固定提交 SHA。发布包只包含分发文件，不上传 obj、日志、测试配置、临时设计稿或本地历史备份。

## 产品标识

WinPPlus 使用统一的窗口类名、互斥体、共享路由映射和 Hook 导出符号。配置只读写 `%APPDATA%/WinPPlus`，不携带原型的名称或配置迁移代码。

正式资源包含选定的 `assets/app-icon.svg`、两套主题 ICO 和可重现图标的 GDI+ 开发脚本。文档截图为实际窗口的原始像素 PNG；`--preview-menu --dry-run` 供截图检查使用，焦点转移时保持预览窗口显示，正常 Win+P 菜单的焦点关闭行为不变。
