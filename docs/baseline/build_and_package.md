# QuickClash 构建与离线包基线

2026-10-24 Windows Release、QML lint/cache、7/7 CTest、独立 headless、完整官方内核探针与部署通过，见 [当前验证](../progress/reports/20261024_windows_private_working_set.md)。G0按Windows首版范围Pass；Linux/macOS原生与CI执行后置P4。专用工作集短测通过，G1因正式矩阵未完成保持Blocked。应用显示名QuickClash、target/命名空间quickclash，源码遵循 [编码规范](../development/cpp_style.md)。

## 依赖与构建

- C++20；CMake ≥3.24，参考机 4.3.0；Ninja 1.12.1；项目接受 Qt ≥6.8，参考/验收环境仍固定 Qt 6.8.0，开发根目录可配置。
- Windows 使用 MSVC 19.50.35727.0（Visual Studio 2026 18.4.1），SDK 10.0.26100.0。Qt 套件名为 msvc2022_64，该参考组合已完整链接/启动通过，不外推其他组合。
- yaml-cpp 0.9.0 正式 release，版本、URL、SHA-256 统一读取 [release_matrix.json](release_matrix.json)。第三方只解到 build/_deps，不改上游 CMakeLists，不全局降低策略。
- 0.8.0 声明 CMake 3.4，触发 CMake 4 删除的旧策略兼容；0.9.0 原文声明 3.5...3.30，现已在 CMake 4.3.0 完整构建和契约测试通过。
- 版本化缓存名避免旧 yaml_cpp.tar.gz 固定旧版本；CMake 从清单读取缓存名和摘要，每次配置刷新 FetchContent 来源。无需删除整个 build 目录。

在仓库根目录执行：

仓库目录统一为 `quick-clash`，实际工作路径为 `D:/repos/quick-clash`，[目录迁移](../progress/reports/20261024_repository_rename.md)及会话写权限更新已完成。移动源码目录后，旧 CMake/Ninja 生成文件含原绝对路径，不能直接复用；保留旧 `build/` 作为历史资料，使用全新目录，例如 `-BuildDirectory build/windows_x64`、`-BuildDirectory build/headless_x64 -Headless`。脚本和 presets 均从当前仓库位置求路径，下载缓存 `.cache/` 可继续按摘要校验使用。

    pwsh -NoProfile -File scripts/fetch_dependencies.ps1
    pwsh -NoProfile -File scripts/build_windows.ps1 -QtRoot C:/Qt/6.8.0/msvc2022_64 -Fresh
    pwsh -NoProfile -File scripts/build_windows.ps1 -QtRoot C:/Qt/6.8.0/msvc2022_64 -Headless -Fresh

上面两条是冻结参考环境的验收命令，不是本地开发唯一入口。`-QtRoot` 可省略；省略时脚本不覆盖 `CMAKE_PREFIX_PATH` 或 PATH 中的 Qt，由 IDE kit、用户 preset 或调用环境负责提供。`-VsInstallPath` 可选择具体 Visual Studio 安装；未指定时优先沿用当前可找到 `cl.exe` 的开发者环境，否则才用 vswhere 选择最新含 x64 C++ 工具的实例。Ninja 默认由 CMake/PATH 查找，也可用 `-Ninja` 显式指定。不同 Qt、编译器或 generator 必须使用独立构建目录或 `-Fresh`，避免复用已有 CMake cache。

脚本仍使用仓库 Ninja preset；可用 `-CMake` 指定 CMake 路径、`-BuildDirectory` 指定独立构建目录。默认使用 PATH 的 cmake，不隐式退到旧版本。Qt Creator/VS Code 可以选择兼容的 Qt ≥6.8 套件并提供 `CMAKE_PREFIX_PATH`，个人机器路径写入不提交的 `CMakeUserPresets.json`；首次先获取依赖，CMake 配置本身不联网。支持/发布结论仍以版本矩阵中的冻结 Qt 6.8.0 环境和真实验收为准。

构建/探针入口要求 PowerShell ≥7.2。`build_windows.ps1` 每次在所选构建目录的 `logs/<UTC 时间>_<运行 ID>/` 保存 configure/build/qmllint/ctest 输出、退出码、耗时、超时标记及 JUnit。配置默认最多 120 秒，构建和 lint 各 600 秒，可通过 `-ConfigureTimeoutSeconds`、`-BuildTimeoutSeconds` 调整；超时仅结束本次创建的进程树。UI 构建自动执行生成模块的 `quickclash_qmllint` 和 CTest，包括 offscreen 软件渲染 smoke；headless 运行契约测试。

本轮 UI 构建目录为 `build/p1_windows`，无 UI 为 `build/p0_headless`。进程执行器测试 `pwsh -NoProfile -File tests/scripts/native_process_test.ps1` 已通过；此测试不编译客户端。早期审批服务失败见历史报告，目前 Windows 原生构建与部署已通过。

Linux/macOS 候选环境由 [CI](../../.github/workflows/build.yml) 固定。先运行 fetch_dependencies.ps1 -YamlOnly，再执行 cmake --preset dev、cmake --build --preset dev、ctest --preset dev。CI 未推送触发，三平台通过证据尚不存在。headless preset 只要求 Qt Core，不显式查找 Network/Quick/Qml/Gui/Widgets，契约目标仅使用 Qt Core 和 domain/config/application；domain/application 不包含 QProcess、窗口和 OS 头。Windows CI 使用同一构建脚本，并获取摘要锁定的官方内核运行完整 API/监听归属/Job 探针；工作流保留 UI/headless 和内核证据，尚未执行。

## 质量命令

    cmake --build build/dev --target quickclash_qmllint
    ctest --preset dev
    pwsh -NoProfile -File scripts/probe_mihomo.ps1 -BuildDirectory build/dev

Windows 才构建 core_job_probe。probe_mihomo -ApiOnly 独立验证控制 API，显式跳过 OS 监听归属/Job，不能作为 P0-04 全部完成证据。完整 probe 需账户可读 TCP 归属；只管理本次创建进程，不修改系统代理。

内核探针默认在 `docs/progress/reports/p0_core/<UTC 时间>_<运行 ID>/` 保存成功或失败记录；可用 `-EvidenceDirectory` 指定新的目录，已有目录拒绝使用，旧记录不覆盖。配置 `-t` 每次限时 10 秒，失败阶段、退出码/超时和清理结果写入 summary/日志。完整探针包含 PID 监听归属、Job 和零网络能力 AppContainer 正反例；隔离首先核对父进程可连接、自有子进程不能连接，使用相同官方程序字节。API-only 仍不能替代这些结果，详见当前报告。

clang-format 21.1.2 处理自有 C++；clang-tidy 21.1.2 配置已可解析，完整分析尚未执行。QML 使用 Qt 6.8.0 qmlformat 和 qmllint target。编译/缓存/模块导出及 --smoke-test、无 UI 契约已经通过，P0-06/08 已关闭。

## P2 离线验证包

P0 定义布局，P2-16 实现在无开发工具的普通用户机器验证，不把空壳叫作离线产品：bin/quickclash.exe、Qt DLL/platforms/styles/TLS/QML imports、core/mihomo.exe、metadata/release_matrix.json、licenses/第三方许可与源码获取说明。

运行 `pwsh -NoProfile -File scripts/deploy_windows.ps1 -BuildDirectory build/p1_windows -Destination dist/p1_validated`，工具不在 PATH 时补 `-CMake`。脚本将 prefix 解析为绝对路径，通过 qt_generate_deploy_qml_app_script 收集 Qt/QML，临时移除开发 Qt PATH 后用随包 Windows 插件执行 smoke，保存输出/退出码。当前部署通过；仍需在 P2-16 核验干净环境 TLS、VC runtime 和发行裁剪。Mihomo 原始字节校验后单独加入，不构建 Go 源码。P2 无 Geo 资源，许可义务见 [台账](licenses.md)。

安装目录只读。数据实现前设置 applicationName=QuickClash，再冻结 QStandardPaths::AppLocalDataLocation，不虚构组织域名。运行文件、元数据、DPAPI 密文、配置分区存放。完整 URL/头、节点凭据、session secret 不入日志/QML/报告；DPAPI 和 session ACL 在 P2-02/04 验证，P0 临时脚本不是生产存储。

正常停止释放 owned 系统代理租约并停止内核。无服务模式 Job 可随宿主结束内核，但不能即时恢复系统代理，P2 须下次启动核对与手工修复。当前 P0 shell 不启动内核、不写代理，尚无代理功能。

## P1 原型的构建与边界

详见 [当前验证](../progress/reports/20261024_windows_private_working_set.md)。Qt Widgets仅用于托盘/原生菜单宿主，Qt Network用于用户范围单实例IPC；界面保持Qt Quick，headless边界不变。CTest的contracts、ui_state、single_instance、ui_prototype、qml_smoke、ui_keyboard、host_lifecycle七项通过；真实Windows托盘和键盘探针也已运行。BUILD_TESTING开启时加入Qt6::Test和键盘探针，正式无测试构建不要求该模块。Windows未显式设置QSG_RENDER_LOOP时默认basic，保留D3D11，修复参考环境threaded重建句柄增长；其他平台不改默认，覆盖循环须重测。

`quickclash --replay` 显示 500 节点/1,000 连接，默认关闭；`--replay-connections=10000` 是探索档，拒绝其他值。`--no-tray` 验证关闭退路；`--isolated` 禁用持久偏好和单实例/托盘，供独立场景。`--host-test` 自动核对无托盘/最小化/卸载恢复；`--tray-test` 只启用真实托盘、不写偏好；`--prototype-test` 输出页面与100次重建诊断。隔离运行可用 `--capture=绝对路径.png --quit-after-ms=2500` 保存页面，结合 `--theme=light|dark --page=home --replay`。

bench/measure_windows.ps1默认100ms采样，通过K32GetProcessMemoryInfo的EX2.PrivateWorkingSetSize判断专用工作集≤100,000,000字节；总工作集/私有提交同时记录但不作为该硬门槛。API失败则采集失败，执行或预算失败保留证据并退出1。字体改用已安装Microsoft YaHei后，当前部署版六场景×两主题短测均通过。正式场景需-WarmupSeconds 120 -Runs 5及设计时长，并补当前采集器尚缺的帧/GPU/滚动数据；短测不证明CPU/长稳或真实转发。隔离截图支持--window-size=720x560；缩放因子不可当作实际DPR。
