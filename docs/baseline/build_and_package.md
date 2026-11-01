# QuickClash 构建与离线包基线

本文维护依赖获取、构建、验证与发行的使用约定。当前任务及验收状态见[进度入口](../progress/README.md)，历史Windows验证见[阶段摘要](../progress/reports/windows_validation.md)。应用显示名QuickClash、target/命名空间quickclash，源码遵循[编码规范](../development/cpp_style.md)。

## 依赖与构建

Windows UI链接后使用所选Qt套件的windeployqt扫描QML，将动态组件、插件及其依赖部署到构建输出目录；服务链接后也复制其运行DLL，使SCM的SYSTEM环境无需开发Qt PATH。CMake生成相对路径qt6.conf，优先于Qt自动生成的构建qt.conf，避免仅复制QtCore后丢失QML/TLS路径。修改Qt套件须使用新的构建目录；构建目录仍不是发行包，独立便携包须使用下述部署脚本，安装只生成匹配bin/qml/plugins布局的qt.conf，不携带开发qt6.conf。

- C++20；CMake ≥3.24，参考机 4.3.0；Ninja 1.12.1；项目接受 Qt ≥6.8，参考/验收环境仍固定 Qt 6.8.0，开发根目录可配置。
- Windows 使用 MSVC 19.50.35727.0（Visual Studio 2026 18.4.1），SDK 10.0.26100.0。Qt 套件名为 msvc2022_64，该参考组合已完整链接/启动通过，不外推其他组合。
- yaml-cpp 0.9.0 正式 release，版本、URL、SHA-256 统一读取 [release_matrix.json](release_matrix.json)。第三方只解到 build/_deps，不改上游 CMakeLists，不全局降低策略。
- 0.8.0 声明 CMake 3.4，触发 CMake 4 删除的旧策略兼容；0.9.0 原文声明 3.5...3.30，现已在 CMake 4.3.0 完整构建和契约测试通过。
- 版本化缓存名避免旧 yaml_cpp.tar.gz 固定旧版本；CMake 从清单读取缓存名和摘要，每次配置刷新 FetchContent 来源。无需删除整个 build 目录。

### 依赖初始化与 Geo 更新边界

默认依赖获取是**获取锁定快照**。Geo使用2026-10-24快照，数据来源固定为官方提交`bfa68b978ec9c0494203d7c0d64e186640f7257b`，许可证固定为`964be7423e659538e4aa4b77ff908188b7102890`；摘要、大小见版本矩阵，校验与运行使用相同字节。上游数据分支会强制推送，固定提交不等于永久托管保证，应保存已验证的离线副本。

仅解除CMake配置的yaml-cpp缺失时，可执行`pwsh -NoProfile -File scripts/fetch_dependencies.ps1 -YamlOnly`；已有缓存可追加`-Offline`验证。省略`-YamlOnly`才准备Mihomo和Geo等运行/打包资源。`-YamlOnly`成功不代表完整运行资源已齐备。

摘要不匹配时，脚本报告来源、文件路径、预期/实际SHA-256；超限报告实际大小和上限。失败下载不发布为有效缓存，并清理本次临时文件；已有缓存即使校验失败也不自动覆盖，应先核实后仅移走明确损坏的单项，不删除整个缓存。缺少离线文件时提示恢复冻结文件或显式联网获取。不要关闭校验、把临时文件改名，或自动接受本次下载摘要。

需要更新Geo时，必须另行核实新快照来源，显式更新日期、固定提交、摘要及大小，并完成受影响的兼容/功能验收；初始化脚本不自动升级数据或改写清单。此次获取修复与验证边界见[报告](../progress/reports/20261027_github_release.md#依赖固定来源)。

获取行为的独立回归入口为`pwsh -NoProfile -File tests/scripts/fetch_dependencies_test.ps1`，使用隔离缓存和模拟网络，不编译产品，也不替代真实资产下载/校验。

在仓库根目录执行：

仓库目录统一为 `quick-clash`，实际工作路径为 `D:/repos/quick-clash`，[目录迁移](../progress/reports/windows_validation.md#工程与内核)及会话写权限更新已完成。移动源码目录后，旧 CMake/Ninja 生成文件含原绝对路径，不能直接复用；保留旧 `build/` 作为历史资料，使用全新目录，例如 `-BuildDirectory build/windows_x64`、`-BuildDirectory build/headless_x64 -Headless`。脚本和 presets 均从当前仓库位置求路径，下载缓存 `.cache/` 可继续按摘要校验使用。

    pwsh -NoProfile -File scripts/fetch_dependencies.ps1
    pwsh -NoProfile -File scripts/build_windows.ps1 -QtRoot C:/Qt/6.8.0/msvc2022_64 -Fresh
    pwsh -NoProfile -File scripts/build_windows.ps1 -QtRoot C:/Qt/6.8.0/msvc2022_64 -Headless -Fresh

上面两条是冻结参考环境的验收命令，不是本地开发唯一入口。`-QtRoot` 可省略；省略时脚本不覆盖 `CMAKE_PREFIX_PATH` 或 PATH 中的 Qt，由 IDE kit、用户 preset 或调用环境负责提供。`-VsInstallPath` 可选择具体 Visual Studio 安装；未指定时优先沿用当前可找到 `cl.exe` 的开发者环境，否则才用 vswhere 选择最新含 x64 C++ 工具的实例。Ninja 默认由 CMake/PATH 查找，也可用 `-Ninja` 显式指定。不同 Qt、编译器或 generator 必须使用独立构建目录或 `-Fresh`，避免复用已有 CMake cache。

脚本仍使用仓库 Ninja preset；可用 `-CMake` 指定 CMake 路径、`-BuildDirectory` 指定独立构建目录。默认使用 PATH 的 cmake，不隐式退到旧版本。Qt Creator/VS Code 可以选择兼容的 Qt ≥6.8 套件并提供 `CMAKE_PREFIX_PATH`，个人机器路径写入不提交的 `CMakeUserPresets.json`；首次先获取依赖，CMake 配置本身不联网。支持/发布结论仍以版本矩阵中的冻结 Qt 6.8.0 环境和真实验收为准。

构建/探针入口要求 PowerShell ≥7.2。`build_windows.ps1` 每次在所选构建目录的 `logs/<UTC 时间>_<运行 ID>/` 保存 configure/build/qmllint/ctest 输出、退出码、耗时、超时标记及 JUnit。配置默认最多120秒，构建/lint和整套CTest各600秒，可通过`-ConfigureTimeoutSeconds`、`-BuildTimeoutSeconds`、`-TestTimeoutSeconds`调整；每个用例仍受CMake中的独立时限约束，超时仅结束本次创建的进程树。UI构建自动执行生成模块的`quickclash_qmllint`和CTest，包括offscreen软件渲染smoke；headless运行契约测试。

默认UI开发构建目录为`build/dev`，headless为`build/headless`，本地独立发行默认`build/release`；Windows CI完成测试后在同一`build/dev`切换为无测试发行配置以复用编译结果，详见下节。进程执行器测试 `pwsh -NoProfile -File tests/scripts/native_process_test.ps1` 不编译客户端，默认在`build/script_tests/native_process/<UTC>_<ID>/`保留环境、参数/输出、退出与超时证据；可指定新的`-EvidenceDirectory`，已有目录拒绝覆盖。Actions即使失败也上传该目录。执行器按UTF-8读取stdout/stderr，子进程必须输出相同编码；测试PowerShell夹具显式设置UTF-8，不依赖Windows OEM代码页。调用其他编码工具可用`Invoke-NativeStep -TextEncoding`指定对应的.NET Encoding，日志元数据记录实际解码方式。

快速迭代阶段，[日常CI](../../.github/workflows/build.yml)当前自动发布门禁仅覆盖Windows：main推送和PR自动执行，其他分支在PR或手动运行时验证，`v*`标签强制完整Windows验证。纯文档提交只运行仓库检查；`docs/baseline/config_policy.json`、`release_matrix.json`及随包`licenses.md`变化仍构建产品。未知变更按产品处理，手动运行始终构建。仓库文档/台账/源码命名、脚本回归和格式检查只跑一个Windows job；同分支新提交取消旧验证，标签验证及发布任务不主动取消。

Windows使用windows-2022/Qt6.8.0/`win64_msvc2022_64`；[独立手动平台验证](../../.github/workflows/platform_validation.yml)使用ubuntu-22.04/`linux_gcc_64`及macos-14/`clang_64`，不阻塞当前Windows发布。这是流水线现状，不代表跨平台产品定位或各平台支持范围。CI先设置Python3.14，再安装PyPI可用的CMake4.3.0、Ninja1.13.2二进制包；仓库检查job另装clang-format21.1.2。Qt Action关闭内部Python设置，固定aqtinstall3.3.0/py7zr1.1.0并缓存Qt。固定YAML/Mihomo/Geo缓存键包含release_matrix摘要，缓存命中仍执行fetch_dependencies重新核验；不缓存跨机器构建目录。本机历史Ninja1.12.1记录不等于PyPI存在同名包版本。

Linux/macOS先运行`fetch_dependencies.ps1 -YamlOnly`，再执行dev原生构建、QML lint及`ctest --preset dev`。CMake仅在Windows注册runtime及其专项、proxy_lease、recovery和runtime_publication，这些测试依赖Windows官方内核、AppContainer或具体适配；其余共享单元/UI及Unsupported契约测试继续执行。非Windows凭据/核心/系统代理/TUN/采样尚未实现，能力明确Unsupported，服务命令不打开IPC；静态核对或Windows上的替代实现测试不能当作原生支持。headless构建及其全部测试另行执行；preset要求Qt Core及Network（DNS地址规范化校验使用QHostAddress，不创建网络请求），不查找Quick/Qml/Gui/Widgets。Qt翻译资源通过`qmake -query QT_INSTALL_TRANSLATIONS`定位，兼容DLL、Linux库和macOS framework布局。

仓库检查执行`pwsh -NoProfile -File scripts/check_platform_boundaries.ps1`，拦截共享层/infra公开头的Windows SDK及具体适配include、功能页面的导航壳依赖；CMake选源/系统库、路径和异步生命周期另行人工核对。Windows实现位于platform/windows，非Windows选择platform/unsupported，共享公开头只暴露Qt/职责契约；SDK库以PRIVATE链接。静态边界、原生构建和真实系统能力分层记录，验收范围见[Windows阶段摘要](../progress/reports/windows_validation.md#跨平台静态边界)。

Windows CI在构建/测试前准备摘要锁定的YAML、Mihomo和Geo。普通运行调用`build_windows.ps1 -Fast`，构建`quickclash_ci_fast`（客户端/服务/快速测试，跳过慢集成程序及独立探针），QML lint后运行CMake注册的快速单元/UI测试；`ci_slow`标签标记runtime、runtime_publication、recovery三项，不修改其断言或时限。默认不带-Fast的本地入口仍构建/测试全部目标。

`v*`标签或手动勾选`full_validation`时，同次Windows运行改为全部CTest、独立headless构建/5项测试和官方内核API/监听归属/Job/AppContainer探针；不先重复运行快速测试。工作流始终上传可用日志/JUnit及aqt安装日志14天。完整模式适用于内核/恢复/存储关键改动和版本发布，快速模式成功只证明该范围，不能当完整端到端或MVP验收。实际结果见[CI核验报告](../progress/reports/20261027_github_release.md#ci故障与修复)与DEVLOG，远端修订版仍须执行留证。

### GitHub持续交付与干净发行包

PR仅验证。默认分支（当前main）Windows快速验证通过后更新GitHub Releases的`continuous`预发行版；`v*`标签完整Windows验证成功后发布对应版本，标签含连字符时为预发行。手动运行默认分支或版本标签遵循同一规则，可用`full_validation`追加完整门槛。Linux/macOS独立手动验证，不是Windows发布依赖。验证任务只读，只有依赖其成功的发行任务获得写Release权限；没有自动提交源码的步骤。continuous仅代表快速验证的开发快照，版本发布还须结合已声明支持范围的验收证据。

Windows验证runner复用已装工具/依赖，测试通过后执行`cmake --preset release -B build/dev`，在同一目录切换到`CMAKE_BUILD_TYPE=Release`、`BUILD_TESTING=OFF`再构建。UI测试探针为仅测试静态库，测试宏/开发资产路径宏只作用于使用它们的源文件；切换时复用未变的公共对象/QML缓存，只重新编译有差异的代码及重新链接，不再启动第二套独立全量构建。目录中残留的测试文件不安装；配置和当前目标图均关闭测试，生产程序不再链接探针/QtTest。本地`cmake --preset release`仍默认独立目录，只有同工具链/同Qt/同源码且已完成测试的CI顺序流程采用目录复用。再次验证须先运行dev配置，不能对发行配置直接执行旧CTest清单。

`scripts/package_release_windows.ps1 -BuildDirectory build/dev`拒绝测试配置，执行安装与清开发Qt PATH启动检查，再加入已核验的官方内核/Geo/许可、元数据、简明随包说明和程序内置的固定动作服务安装入口（不附带 PowerShell 安装脚本）。ZIP与摘要作为同次运行的`windows_package` artifact交给仅发布job；该job不安装Qt、不重新构建，发布前再校验ZIP摘要。Qt部署负责收集Qt插件/QML运行资源，CMake收集可再分发CRT DLL，关闭vc_redist安装器部署；不复制整个build或.cache。包布局仍为`bin/`、`core/`、`geo/`、`licenses/`、`metadata/`。

Release资产为`quickclash_windows_x64.zip`及`.zip.sha256`，清单记录版本/commit/构建标志/摘要/签名状态。测试exe、QtTest、源码/头文件、静态库、符号、构建与测试日志均拒绝进入dist；Actions日志artifact单独保存。当前为未签名便携包，自动发布不等于干净机器/TUN或应用内自动更新验收通过。

ZIP解压目录是程序、服务、核心和随包Geo的唯一发行来源。首次TUN请求UAC，SCM直接注册bin/quickclash_service.exe并设为自动启动；不在Program Files复制运行文件。后续重开/同目录更新免UAC；更新服务文件前先停止服务，移动目录须再次授权。ProgramData仅保留授权、私有会话及清理日志。

本地核对命令（首次先完整获取依赖）：`cmake --preset release`、`cmake --build --preset release --parallel 4`、`pwsh -NoProfile -File scripts/package_release_windows.ps1 -Version continuous`。已有dist不覆盖，复核时显式选择新的`-Destination`。历史`package_windows.ps1`是本地验证包入口，不能作为自动发行入口。

## 质量命令

Windows UI目标通过resources/quickclash.rc编译嵌入灵猫Q图标，资源管理器及快捷方式直接使用EXE内的ICO；运行时窗口使用同一ICO，托盘使用同源明暗单色SVG。resources/icons/brand/quickclash.ico包含16至256像素九档尺寸，随源码保存，常规CMake构建无需Node。修改品牌SVG后，以安装sharp的开发环境运行node scripts/build_brand_icon.cjs，再运行同命令--check；该生成工具及sharp不进入运行包。导航和收展使用本地Lucide SVG，QtSvg解码器及许可随包部署。

    cmake --build build/dev --target quickclash_qmllint
    ctest --preset dev
    pwsh -NoProfile -File scripts/probe_mihomo.ps1 -BuildDirectory build/dev

Windows 才构建 core_job_probe。probe_mihomo -ApiOnly 独立验证控制 API，显式跳过 OS 监听归属/Job，不能作为 P0-04 全部完成证据。完整 probe 需账户可读 TCP 归属；只管理本次创建进程，不修改系统代理。

内核探针默认在 Git忽略的 `build/core_probe/<UTC 时间>_<运行 ID>/` 保存成功或失败记录；可用 `-EvidenceDirectory` 指定新的目录，已有目录拒绝使用，旧记录不覆盖。配置 `-t` 每次限时 10 秒，失败阶段、退出码/超时和清理结果写入 summary/日志。完整探针包含 PID 监听归属、Job 和零网络能力 AppContainer 正反例；隔离首先核对父进程可连接、自有子进程不能连接，使用相同官方程序字节。API-only 仍不能替代这些结果。原始输出不提交到reports，Actions日志artifact保留14天；长期证据按[留存规则](../progress/reports/README.md)精选。

clang-format 21.1.2 处理自有 C++；clang-tidy 21.1.2 配置已可解析，完整分析尚未执行。QML 使用 Qt 6.8.0 qmlformat 和 qmllint target。编译/缓存/模块导出及 --smoke-test、无 UI 契约已经通过，P0-06/08 已关闭。

## P2 离线验证包

P0定义布局，P2-16交付本机完整离线包并核对清开发Qt PATH后的本地导入/Ready，P3-14在G3前补无Qt/开发工具的普通用户干净环境；清PATH不代表干净机器。包包括bin/quickclash.exe、Qt DLL/platforms/styles/TLS/QML imports、CRT、core/mihomo.exe、metadata/release_matrix.json、licenses/项目GPL-3.0-only与第三方许可、源码获取说明，已声明固定Geo资源也须随包。范围见[G2复核](../progress/reports/windows_validation.md#阶段范围与待补项)。

运行 `pwsh -NoProfile -File scripts/deploy_windows.ps1 -BuildDirectory build/p1_windows -Destination dist/p1_validated`，工具不在PATH时补-CMake。脚本将prefix解析为绝对路径，通过qt_generate_deploy_qml_app_script收集Qt/QML，移除开发Qt PATH后用随包Windows插件执行smoke并留结果。P2-16核对完整依赖/CRT/内核及启动证据，P3-14补干净环境TLS/运行路径；Mihomo字节校验后单独加入，不构建Go源码。当前固定Geo范围见P3-10，许可义务见[台账](licenses.md)。

P2完整打包入口：pwsh -NoProfile -File scripts/package_windows.ps1 -BuildDirectory build/p1_windows -Destination dist/p2_final -CMake 'C:/Program Files/CMake/bin/cmake.exe'。默认读取固定官方内核缓存并核验摘要，显式加入项目GPL-3.0-only许可、VC CRT、官方/Qt/YAML许可、release_matrix、用户指南和文件摘要；目标目录必须尚不存在。Mihomo许可证文本路径可用-MihomoLicense指定，须为固定版本原文。当前包是本地开发验证，BUILD_TESTING探针开启、未签名；干净普通用户环境由P3-14在G3前验收，未按本机清PATH放行。

安装目录只读，applicationName=QuickClash；实际数据路径为QStandardPaths::AppConfigLocation/QuickClash对应位置，以设置页展示值为准，不再使用未实现的AppLocalDataLocation约定。运行文件、元数据、DPAPI密文、原文/语义/会话配置分区存放。订阅URL/头与测试目标使用DPAPI；节点凭据存在受限ACL文件，预览遮蔽，日志不输出原始内核内容。P1 ui.ini偏好迁移后保留原件。

停止先释放owned系统代理租约再终止owned Job；当前不发正常退出请求。无服务模式Job随宿主结束内核，但强杀时不能即时恢复系统代理，下次启动先按持久租约核对匹配值，外部变化不覆盖，手工说明见随包指南。系统代理意图独立持久化，退出保留、明确关闭清除；恢复完成后后台运行有效配置并按已确认意图申请代理，不注册系统开机启动。独立真实runtime worker强杀→Job退出/释放端口→下次WinINet租约与未提交事务恢复已通过；GUI进程整体强杀及干净环境另记，见[G2恢复报告](../progress/reports/windows_validation.md#恢复与本地包)。

## Windows验收工具包

测试构建离线包可运行tests/scripts/desktop_package_test.ps1 -PackageRoot '<完整包>' -Phase Package、Host、Flow或Proxy。Package核对全包摘要/x64和清开发Qt环境后的本地导入/Ready及缺核心修复；Host核对真实托盘隐藏卸载/转发、整GUI强杀与单实例重开。Flow覆盖GUI订阅/节点/模式、显式及WinINet客户端HTTPS转发与退出重开，会临时申请当前用户系统代理。Proxy会临时启用当前用户WinINet代理并用独立进程改绕过列表，核对冲突/新基线和原快照还原，仅在当前值仍属于本轮测试时恢复，不覆盖后续外部修改。原始证据默认写build/新目录，显式标记隔离数据，不使用个人配置；此探针仅BUILD_TESTING开启，不能代替干净机器、真实换网或TUN。包内metadata/release_matrix.json存在时核心只按随包路径解析，缺件不得回退开发缓存。

将当前测试包准备成无需源码仓库的独立目录：pwsh -NoProfile -File scripts/create_windows_validation_kit.ps1 -PackageRoot '<完整测试包>' -Destination 'build/windows_validation_kit'。目标必须不存在且在源包目录之外；复制前核对原包摘要，保留原manifest，带上必要脚本、公开回环TLS证书/私钥及[工具包说明](../../resources/windows_validation_readme.md)，另生成validation_manifest.json用于发现搬运损坏，不构成签名。复制整个目录后在工具包根目录运行上述脚本，-PackageRoot使用'./package'。目标桌面需要Windows x64和PowerShell 7.2+（可用官方便携版），无需Qt SDK或VS；该要求仅用于验收脚本。产品服务安装不依赖PowerShell；以普通用户运行，验收夹具本身不要求安装权限服务，显式测试 TUN 时由应用请求一次 UAC。测试证书仅进程内信任；本机工具验证不等于干净机器验收。

## P1 原型的构建与边界

详见 [当前验证](../progress/reports/windows_validation.md#原型生命周期与资源)。Qt Widgets仅用于托盘/原生菜单宿主，Qt Network用于用户范围单实例IPC；界面保持Qt Quick，headless边界不变。CTest的contracts、ui_state、single_instance、ui_prototype、qml_smoke、ui_keyboard、host_lifecycle七项通过；真实Windows托盘和键盘探针也已运行。BUILD_TESTING开启时加入Qt6::Test和键盘探针，正式无测试构建不要求该模块。Windows未显式设置QSG_RENDER_LOOP时默认basic，保留D3D11，修复参考环境threaded重建句柄增长；其他平台不改默认，覆盖循环须重测。

Qt6.8本机basic/D3D11帧延迟等待对象会在连续缩放中阻塞GUI线程约一秒。P2-24在默认basic且空/d3d11 RHI、无QT_QUICK_BACKEND和显式D3D选项时，启动前设置QT_D3D_MAX_FRAME_LATENCY=0；保留GPU、flip呈现和垂直同步。QT_D3D_MAX_FRAME_LATENCY、QT_D3D_NO_FLIP若已设置则尊重调用者，不全局改软件渲染或threaded；其他平台不设置。该选项只在进程启动前生效，试用dist/p2_resize须正常退出旧QuickClash后启动新包，单实例激活旧进程不能加载新实现。

`quickclash --replay` 显示 500 节点/1,000 连接，默认关闭；`--replay-connections=10000` 是探索档，拒绝其他值。`--no-tray` 验证关闭退路；`--isolated` 禁用持久偏好和单实例/托盘，供独立场景。`--host-test` 自动核对无托盘/最小化/卸载恢复；`--tray-test` 只启用真实托盘、不写偏好；`--prototype-test` 输出页面与100次重建诊断。隔离运行可用 `--capture=绝对路径.png --quit-after-ms=2500` 保存页面，结合 `--theme=light|dark --page=home --replay`。

bench/measure_windows.ps1默认首页、浅色主题、3秒预热、30秒单轮、100ms采样，通过K32GetProcessMemoryInfo的EX2直接采集专用工作集，同时记录总工作集/私有提交，不设固定字节门槛。摘要将执行结果execution_result与内存采集结果memory_measurement_result分开，后者为Sampled/NotRun，不再输出memory_budget_result；采样完成不自动表示资源优化通过。API失败或执行/生命周期失败保留证据并退出1。bench/measure_runtime_windows.ps1用于实际Ready内核首页短测，result=Sampled表示完成采样，三项内核指标单列。按1.6策略，日常只在资源敏感变化时选一个场景，3秒预热/10—30秒/1次即可；不要求120秒预热/5次重复或帧/GPU/滚动采集。需要专项时显式选择-Theme all -Scenario all，普通交付不运行完整矩阵。关键历史结论及精选证据按留存规则保留，原始输出写build/或CI artifacts；短测不证明CPU/长稳或真实转发。隔离截图支持--window-size=720x560；缩放因子不可当作实际DPR。

本地验证包须保留完整目录，退出旧实例后再启动新包；历史包标识、摘要及已测范围统一见[恢复与本地包](../progress/reports/windows_validation.md#恢复与本地包)。本机清PATH启动、服务程序随包均不表示干净普通用户环境或TUN验收通过。
