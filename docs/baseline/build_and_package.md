# QuickClash 构建与离线包基线

2026-10-24 P2已通过Windows Release增量构建、10/10 CTest、独立headless2/2、真实CONNECT/TLS与WinINet系统代理恢复；QML lint退出0但Qt6.8 Dialogs仍有重复类型导入警告。完整本地验证包及真实GUI+Ready内核短测已具备，见 [P2交付](../progress/reports/20261024_p2_implementation.md)。G0/G1保留基线Pass，G2 Blocked；长时/帧/GPU专项和Linux/macOS原生/CI执行后置。应用显示名QuickClash、target/命名空间quickclash，源码遵循 [编码规范](../development/cpp_style.md)。

最新缩放修复构建目录build/p2_responsive，完整本地包dist/p2_resize；Release与QML lint、4/4受影响UI检查、真实窗口连续缩放及一次10秒Ready首页内存短测通过，详见[缩放报告](../progress/reports/20261024_resize_layout.md)。上段完整矩阵保留历史范围，本轮未重复网络/恢复矩阵，未扩大G2结论。

2026-10-25最新功能包为dist/p3_subscription，沿用build/p2_responsive增量构建，新增固定Geo与内部DNS。4/4受影响测试及真实代表订阅VLESS HTTPS转发通过，Geo异常拒绝、随包smoke和单轮10秒DNS/Geo Ready首页资源结果见[P3-10报告](../progress/reports/20261024_p3_subscription.md)。fetch_dependencies同时验证Geo数据和许可；上游latest可变，必须匹配release_matrix冻结摘要，离线缓存可复用。校验/运行使用相同Geo字节，不联网下载资源。此增补不宣称G2/G3完整通过。

## 依赖与构建

- C++20；CMake ≥3.24，参考机 4.3.0；Ninja 1.12.1；项目接受 Qt ≥6.8，参考/验收环境仍固定 Qt 6.8.0，开发根目录可配置。
- Windows 使用 MSVC 19.50.35727.0（Visual Studio 2026 18.4.1），SDK 10.0.26100.0。Qt 套件名为 msvc2022_64，该参考组合已完整链接/启动通过，不外推其他组合。
- yaml-cpp 0.9.0 正式 release，版本、URL、SHA-256 统一读取 [release_matrix.json](release_matrix.json)。第三方只解到 build/_deps，不改上游 CMakeLists，不全局降低策略。
- 0.8.0 声明 CMake 3.4，触发 CMake 4 删除的旧策略兼容；0.9.0 原文声明 3.5...3.30，现已在 CMake 4.3.0 完整构建和契约测试通过。
- 版本化缓存名避免旧 yaml_cpp.tar.gz 固定旧版本；CMake 从清单读取缓存名和摘要，每次配置刷新 FetchContent 来源。无需删除整个 build 目录。

### 依赖初始化与 Geo 更新边界

2026-10-26获取修复替代上文历史记录中的latest下载方式：默认依赖获取是**获取锁定快照**，不是升级至上游最新版。Geo沿用2026-10-24快照，数据来源固定为官方提交`bfa68b978ec9c0494203d7c0d64e186640f7257b`，许可证固定为`964be7423e659538e4aa4b77ff908188b7102890`；原摘要、大小和数据字节不变。上游数据分支会强制推送，固定提交不等于永久托管保证，应保存已验证的离线副本。

仅解除CMake配置的yaml-cpp缺失时，可执行`pwsh -NoProfile -File scripts/fetch_dependencies.ps1 -YamlOnly`；已有缓存可追加`-Offline`验证。省略`-YamlOnly`才准备Mihomo和Geo等运行/打包资源。`-YamlOnly`成功不代表完整运行资源已齐备。

摘要不匹配时，脚本报告来源、文件路径、预期/实际SHA-256；超限报告实际大小和上限。失败下载不发布为有效缓存，并清理本次临时文件；已有缓存即使校验失败也不自动覆盖，应先核实后仅移走明确损坏的单项，不删除整个缓存。缺少离线文件时提示恢复冻结文件或显式联网获取。不要关闭校验、把临时文件改名，或自动接受本次下载摘要。

需要更新Geo时，必须另行核实新快照来源，显式更新日期、固定提交、摘要及大小，并完成受影响的兼容/功能验收；初始化脚本不自动升级数据或改写清单。此次获取修复与验证边界见[报告](../progress/reports/20261026_dependency_fetch_fix.md)。

获取行为的独立回归入口为`pwsh -NoProfile -File tests/scripts/fetch_dependencies_test.ps1`，使用隔离缓存和模拟网络，不编译产品，也不替代真实资产下载/校验。

在仓库根目录执行：

仓库目录统一为 `quick-clash`，实际工作路径为 `D:/repos/quick-clash`，[目录迁移](../progress/reports/20261024_repository_rename.md)及会话写权限更新已完成。移动源码目录后，旧 CMake/Ninja 生成文件含原绝对路径，不能直接复用；保留旧 `build/` 作为历史资料，使用全新目录，例如 `-BuildDirectory build/windows_x64`、`-BuildDirectory build/headless_x64 -Headless`。脚本和 presets 均从当前仓库位置求路径，下载缓存 `.cache/` 可继续按摘要校验使用。

    pwsh -NoProfile -File scripts/fetch_dependencies.ps1
    pwsh -NoProfile -File scripts/build_windows.ps1 -QtRoot C:/Qt/6.8.0/msvc2022_64 -Fresh
    pwsh -NoProfile -File scripts/build_windows.ps1 -QtRoot C:/Qt/6.8.0/msvc2022_64 -Headless -Fresh

上面两条是冻结参考环境的验收命令，不是本地开发唯一入口。`-QtRoot` 可省略；省略时脚本不覆盖 `CMAKE_PREFIX_PATH` 或 PATH 中的 Qt，由 IDE kit、用户 preset 或调用环境负责提供。`-VsInstallPath` 可选择具体 Visual Studio 安装；未指定时优先沿用当前可找到 `cl.exe` 的开发者环境，否则才用 vswhere 选择最新含 x64 C++ 工具的实例。Ninja 默认由 CMake/PATH 查找，也可用 `-Ninja` 显式指定。不同 Qt、编译器或 generator 必须使用独立构建目录或 `-Fresh`，避免复用已有 CMake cache。

脚本仍使用仓库 Ninja preset；可用 `-CMake` 指定 CMake 路径、`-BuildDirectory` 指定独立构建目录。默认使用 PATH 的 cmake，不隐式退到旧版本。Qt Creator/VS Code 可以选择兼容的 Qt ≥6.8 套件并提供 `CMAKE_PREFIX_PATH`，个人机器路径写入不提交的 `CMakeUserPresets.json`；首次先获取依赖，CMake 配置本身不联网。支持/发布结论仍以版本矩阵中的冻结 Qt 6.8.0 环境和真实验收为准。

构建/探针入口要求 PowerShell ≥7.2。`build_windows.ps1` 每次在所选构建目录的 `logs/<UTC 时间>_<运行 ID>/` 保存 configure/build/qmllint/ctest 输出、退出码、耗时、超时标记及 JUnit。配置默认最多120秒，构建/lint和整套CTest各600秒，可通过`-ConfigureTimeoutSeconds`、`-BuildTimeoutSeconds`、`-TestTimeoutSeconds`调整；每个用例仍受CMake中的独立时限约束，超时仅结束本次创建的进程树。UI构建自动执行生成模块的`quickclash_qmllint`和CTest，包括offscreen软件渲染smoke；headless运行契约测试。

当前UI增量构建目录为`build/p2_responsive`，P0/P1历史目录为`build/p1_windows`、`build/p0_headless`。进程执行器测试 `pwsh -NoProfile -File tests/scripts/native_process_test.ps1` 已通过；此测试不编译客户端。早期审批服务失败见历史报告，目前 Windows 原生构建与部署已通过。

Linux/macOS 候选环境由 [CI](../../.github/workflows/build.yml) 固定：ubuntu-22.04使用Qt6.8.0的`linux_gcc_64`，macos-14使用`clang_64`；Windows为windows-2022/`win64_msvc2022_64`。CI先设置Python3.14，再安装PyPI可用的CMake4.3.0、Ninja1.13.2、clang-format21.1.2二进制包；Qt Action关闭内部Python设置，并固定aqtinstall3.3.0/py7zr1.1.0。本机历史Ninja1.12.1记录不等于PyPI存在同名包版本。

Linux/macOS先运行`fetch_dependencies.ps1 -YamlOnly`，再执行dev原生构建、QML lint及`ctest --preset dev --exclude-regex '^runtime$'`；runtime集成测试依赖Windows官方内核和AppContainer，不能将非Windows占位适配当作已支持。其余单元/UI测试继续执行，headless构建及其全部测试另行执行。headless preset只要求Qt Core，不显式查找Network/Quick/Qml/Gui/Widgets。Qt翻译资源通过`qmake -query QT_INSTALL_TRANSLATIONS`定位，兼容DLL、Linux库和macOS framework布局。

Windows CI在构建/测试前准备摘要锁定的YAML、Mihomo和Geo，然后使用同一本机构建脚本运行UI/headless构建、QML lint、全部测试及官方内核API/监听归属/Job探针。工作流始终上传可用的构建/测试日志及aqt安装日志。用户已报告三平台初始化失败；配置修复及本地验证见[CI核验报告](../progress/reports/20261027_github_ci_fix.md)，远端修复版重跑及Linux/macOS实际构建仍为NotRun，不宣称三平台通过。

### GitHub持续交付与干净发行包

PR仅验证。默认分支（当前main）全部native矩阵通过后更新GitHub Releases的`continuous`预发行版；`v*`标签运行成功后发布对应版本，标签含连字符时为预发行。手动运行默认分支或版本标签遵循同一规则。Windows x64先发行，Linux/macOS继续构建验证。验证任务只读，只有依赖其成功的发行任务获得写Release权限；没有自动提交源码的步骤。

发行任务重新使用独立`release` preset，`CMAKE_BUILD_TYPE=Release`、`BUILD_TESTING=OFF`，编译期移除宿主/键盘/原型/界面测试探针，不使用dev exe打包。`scripts/package_release_windows.ps1`拒绝测试构建，执行安装与清开发Qt PATH启动检查，再加入已核验的官方内核/Geo/许可、元数据、简明随包说明和可选服务安装脚本。Qt部署负责收集Qt插件/QML运行资源，CMake收集可再分发CRT DLL，关闭vc_redist安装器部署；不复制整个build或.cache。包布局仍为`bin/`、`core/`、`geo/`、`licenses/`、`metadata/`。

Release资产为`quickclash_windows_x64.zip`及`.zip.sha256`，清单记录版本/commit/构建标志/摘要/签名状态。测试exe、QtTest、源码/头文件、静态库、符号、构建与测试日志均拒绝进入dist；Actions日志artifact单独保存。当前为未签名便携包，自动发布不等于干净机器/TUN或应用内自动更新验收通过。

本地核对命令（首次先完整获取依赖）：`cmake --preset release`、`cmake --build --preset release --parallel 4`、`pwsh -NoProfile -File scripts/package_release_windows.ps1 -Version continuous`。已有dist不覆盖，复核时显式选择新的`-Destination`。历史`package_windows.ps1`是本地验证包入口，不能作为自动发行入口。

## 质量命令

    cmake --build build/dev --target quickclash_qmllint
    ctest --preset dev
    pwsh -NoProfile -File scripts/probe_mihomo.ps1 -BuildDirectory build/dev

Windows 才构建 core_job_probe。probe_mihomo -ApiOnly 独立验证控制 API，显式跳过 OS 监听归属/Job，不能作为 P0-04 全部完成证据。完整 probe 需账户可读 TCP 归属；只管理本次创建进程，不修改系统代理。

内核探针默认在 `docs/progress/reports/p0_core/<UTC 时间>_<运行 ID>/` 保存成功或失败记录；可用 `-EvidenceDirectory` 指定新的目录，已有目录拒绝使用，旧记录不覆盖。配置 `-t` 每次限时 10 秒，失败阶段、退出码/超时和清理结果写入 summary/日志。完整探针包含 PID 监听归属、Job 和零网络能力 AppContainer 正反例；隔离首先核对父进程可连接、自有子进程不能连接，使用相同官方程序字节。API-only 仍不能替代这些结果，详见当前报告。

clang-format 21.1.2 处理自有 C++；clang-tidy 21.1.2 配置已可解析，完整分析尚未执行。QML 使用 Qt 6.8.0 qmlformat 和 qmllint target。编译/缓存/模块导出及 --smoke-test、无 UI 契约已经通过，P0-06/08 已关闭。

## P2 离线验证包

P0定义布局，P2-16交付本机完整离线包并核对清开发Qt PATH后的本地导入/Ready，P3-14在G3前补无Qt/开发工具的普通用户干净环境；清PATH不代表干净机器。包包括bin/quickclash.exe、Qt DLL/platforms/styles/TLS/QML imports、CRT、core/mihomo.exe、metadata/release_matrix.json、licenses/第三方许可与源码获取说明，已声明固定Geo资源也须随包。范围见[G2复核](../progress/reports/20261025_g2_scope_review.md)。

运行 `pwsh -NoProfile -File scripts/deploy_windows.ps1 -BuildDirectory build/p1_windows -Destination dist/p1_validated`，工具不在PATH时补-CMake。脚本将prefix解析为绝对路径，通过qt_generate_deploy_qml_app_script收集Qt/QML，移除开发Qt PATH后用随包Windows插件执行smoke并留结果。P2-16核对完整依赖/CRT/内核及启动证据，P3-14补干净环境TLS/运行路径；Mihomo字节校验后单独加入，不构建Go源码。当前固定Geo范围见P3-10，许可义务见[台账](licenses.md)。

P2完整打包入口：pwsh -NoProfile -File scripts/package_windows.ps1 -BuildDirectory build/p1_windows -Destination dist/p2_final -CMake 'C:/Program Files/CMake/bin/cmake.exe'。默认读取固定官方内核缓存并核验摘要，显式加入VC CRT、官方/Qt/YAML许可、release_matrix、用户指南和文件摘要；目标目录必须尚不存在。Mihomo许可证文本路径可用-MihomoLicense指定，须为固定版本原文。当前包是本地开发验证，BUILD_TESTING探针开启、未签名；干净普通用户环境由P3-14在G3前验收，未按本机清PATH放行。

安装目录只读，applicationName=QuickClash；实际数据路径为QStandardPaths::AppConfigLocation/QuickClash对应位置，以设置页展示值为准，不再使用未实现的AppLocalDataLocation约定。运行文件、元数据、DPAPI密文、原文/语义/会话配置分区存放。订阅URL/头与测试目标使用DPAPI；节点凭据存在受限ACL文件，预览遮蔽，日志不输出原始内核内容。P1 ui.ini偏好迁移后保留原件。

停止先释放owned系统代理租约再终止owned Job；当前不发正常退出请求。无服务模式Job随宿主结束内核，但强杀时不能即时恢复系统代理，下次启动先按持久租约核对匹配值，外部变化不覆盖，手工说明见随包指南。系统代理意图独立持久化，退出保留、明确关闭清除；恢复完成后后台运行有效配置并按已确认意图申请代理，不注册系统开机启动。独立真实runtime worker强杀→Job退出/释放端口→下次WinINet租约与未提交事务恢复已通过；GUI进程整体强杀及干净环境另记，见[G2恢复报告](../progress/reports/20261025_g2_recovery.md)。

## P1 原型的构建与边界

详见 [当前验证](../progress/reports/20261024_windows_private_working_set.md)。Qt Widgets仅用于托盘/原生菜单宿主，Qt Network用于用户范围单实例IPC；界面保持Qt Quick，headless边界不变。CTest的contracts、ui_state、single_instance、ui_prototype、qml_smoke、ui_keyboard、host_lifecycle七项通过；真实Windows托盘和键盘探针也已运行。BUILD_TESTING开启时加入Qt6::Test和键盘探针，正式无测试构建不要求该模块。Windows未显式设置QSG_RENDER_LOOP时默认basic，保留D3D11，修复参考环境threaded重建句柄增长；其他平台不改默认，覆盖循环须重测。

Qt6.8本机basic/D3D11帧延迟等待对象会在连续缩放中阻塞GUI线程约一秒。P2-24在默认basic且空/d3d11 RHI、无QT_QUICK_BACKEND和显式D3D选项时，启动前设置QT_D3D_MAX_FRAME_LATENCY=0；保留GPU、flip呈现和垂直同步。QT_D3D_MAX_FRAME_LATENCY、QT_D3D_NO_FLIP若已设置则尊重调用者，不全局改软件渲染或threaded；其他平台不设置。该选项只在进程启动前生效，试用dist/p2_resize须正常退出旧QuickClash后启动新包，单实例激活旧进程不能加载新实现。

`quickclash --replay` 显示 500 节点/1,000 连接，默认关闭；`--replay-connections=10000` 是探索档，拒绝其他值。`--no-tray` 验证关闭退路；`--isolated` 禁用持久偏好和单实例/托盘，供独立场景。`--host-test` 自动核对无托盘/最小化/卸载恢复；`--tray-test` 只启用真实托盘、不写偏好；`--prototype-test` 输出页面与100次重建诊断。隔离运行可用 `--capture=绝对路径.png --quit-after-ms=2500` 保存页面，结合 `--theme=light|dark --page=home --replay`。

bench/measure_windows.ps1默认首页、浅色主题、3秒预热、30秒单轮、100ms采样，通过K32GetProcessMemoryInfo的EX2直接采集专用工作集，同时记录总工作集/私有提交，不设固定字节门槛。摘要将执行结果execution_result与内存采集结果memory_measurement_result分开，后者为Sampled/NotRun，不再输出memory_budget_result；采样完成不自动表示资源优化通过。API失败或执行/生命周期失败保留证据并退出1。bench/measure_runtime_windows.ps1用于实际Ready内核首页短测，result=Sampled表示完成采样，三项内核指标单列。按1.6策略，日常只在资源敏感变化时选一个场景，3秒预热/10—30秒/1次即可；不要求120秒预热/5次重复或帧/GPU/滚动采集。需要专项时显式选择-Theme all -Scenario all，普通交付不运行完整矩阵。历史原始结果按当时策略保留；短测不证明CPU/长稳或真实转发。隔离截图支持--window-size=720x560；缩放因子不可当作实际DPR。

2026-10-25当前本机验证包：dist/g2_recovery/bin/quickclash.exe，整个目录一起使用；SHA-256 9b26165aeebf4c913f2de75d73ad9ef133b04d00b3038357f8dba0c467ad5fac。186文件摘要、清Qt PATH随包smoke、原生runtime转发/恢复及一次10秒Ready首页资源检查通过，见[G2恢复报告](../progress/reports/20261025_g2_recovery.md)。服务程序随包但本轮没有安装或验证TUN。历史P3-11包/私有单节点记录保留，不能代替当前完整P3验收。仍为未签名本地验证包，干净普通用户及正式发行要求单列。
