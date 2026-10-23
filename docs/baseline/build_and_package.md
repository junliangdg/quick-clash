# QuickClash 构建与离线包基线

P0 工作区实现，尚待完整原生构建验证。执行状态见 [P0 报告](../progress/reports/p0_implementation.md)，命令不能代替执行结果。应用显示名 QuickClash、target/命名空间 quickclash，源码遵循 [编码规范](../development/cpp_style.md)。

## 依赖与构建

- C++20；CMake ≥3.24，参考机 4.3.0；Ninja 1.12.1；项目接受 Qt ≥6.8，参考/验收环境仍固定 Qt 6.8.0，开发根目录可配置。
- Windows 实际探测到 MSVC 19.50.35727.0（Visual Studio 2026 18.4.1），SDK 10.0.26100.0。Qt 套件名为 msvc2022_64，最终 ABI/链接/启动兼容性需要完整构建验证。
- yaml-cpp 0.9.0 正式 release，版本、URL、SHA-256 统一读取 [release_matrix.json](release_matrix.json)。第三方只解到 build/_deps，不改上游 CMakeLists，不全局降低策略。
- 0.8.0 声明 CMake 3.4，触发 CMake 4 删除的旧策略兼容；0.9.0 原文声明 3.5...3.30。4.3.0 的 cmake -P 已接受下载包中原样提取的策略声明，这不是完整构建证据。
- 版本化缓存名避免旧 yaml_cpp.tar.gz 固定旧版本；CMake 从清单读取缓存名和摘要，每次配置刷新 FetchContent 来源。无需删除整个 build 目录。

在仓库根目录执行：

    pwsh -NoProfile -File scripts/fetch_dependencies.ps1
    pwsh -NoProfile -File scripts/build_windows.ps1 -QtRoot C:/Qt/6.8.0/msvc2022_64 -Fresh
    pwsh -NoProfile -File scripts/build_windows.ps1 -QtRoot C:/Qt/6.8.0/msvc2022_64 -Headless -Fresh

上面两条是冻结参考环境的验收命令，不是本地开发唯一入口。`-QtRoot` 可省略；省略时脚本不覆盖 `CMAKE_PREFIX_PATH` 或 PATH 中的 Qt，由 IDE kit、用户 preset 或调用环境负责提供。`-VsInstallPath` 可选择具体 Visual Studio 安装；未指定时优先沿用当前可找到 `cl.exe` 的开发者环境，否则才用 vswhere 选择最新含 x64 C++ 工具的实例。Ninja 默认由 CMake/PATH 查找，也可用 `-Ninja` 显式指定。不同 Qt、编译器或 generator 必须使用独立构建目录或 `-Fresh`，避免复用已有 CMake cache。

脚本仍使用仓库 Ninja preset；可用 `-CMake` 指定 CMake 路径、`-BuildDirectory` 指定独立构建目录。默认使用 PATH 的 cmake，不隐式退到旧版本。Qt Creator/VS Code 可以选择兼容的 Qt ≥6.8 套件并提供 `CMAKE_PREFIX_PATH`，个人机器路径写入不提交的 `CMakeUserPresets.json`；首次先获取依赖，CMake 配置本身不联网。支持/发布结论仍以版本矩阵中的冻结 Qt 6.8.0 环境和真实验收为准。

Linux/macOS 候选环境由 [CI](../../.github/workflows/build.yml) 固定。先运行 fetch_dependencies.ps1 -YamlOnly，再执行 cmake --preset dev、cmake --build --preset dev、ctest --preset dev。CI 未推送触发，三平台通过证据尚不存在。headless preset 不查找 Quick/Qml/Gui/Widgets，契约目标仅使用 Qt Core 和 domain/config/application；domain/application 不包含 QProcess、窗口和 OS 头。

## 质量命令

    cmake --build build/dev --target quickclash_qmllint
    ctest --preset dev
    pwsh -NoProfile -File scripts/probe_mihomo.ps1 -BuildDirectory build/dev

Windows 才构建 core_job_probe。probe_mihomo -ApiOnly 独立验证控制 API，显式跳过 OS 监听归属/Job，不能作为 P0-04 全部完成证据。完整 probe 需账户可读 TCP 归属；只管理本次创建进程，不修改系统代理。

clang-format 21.1.2 处理自有 C++；clang-tidy 21.1.2 配置已可解析，实际分析需导入 MSVC 环境后读取 build/dev/compile_commands.json。QML 使用 Qt 6.8.0 qmlformat 和 qmllint target。编译/缓存/模块导出及 --smoke-test 实际通过后才能关闭 P0-06/08。

## P2 离线验证包

P0 定义布局，P2-16 实现在无开发工具的普通用户机器验证，不把空壳叫作离线产品：bin/quickclash.exe、Qt DLL/platforms/styles/TLS/QML imports、core/mihomo.exe、metadata/release_matrix.json、licenses/第三方许可与源码获取说明。

cmake --install build/dev --prefix dist/p0 通过 qt_generate_deploy_qml_app_script 收集 Qt/QML；检查 qwindows、Basic、QML 模块、TLS 后端和 MSVC runtime。Mihomo 原始字节校验后单独加入，不构建 Go 源码。P2 无 Geo 资源，不依赖在线更新，许可义务见 [台账](licenses.md)。

安装目录只读。数据实现前设置 applicationName=QuickClash，再冻结 QStandardPaths::AppLocalDataLocation，不虚构组织域名。运行文件、元数据、DPAPI 密文、配置分区存放。完整 URL/头、节点凭据、session secret 不入日志/QML/报告；DPAPI 和 session ACL 在 P2-02/04 验证，P0 临时脚本不是生产存储。

正常停止释放 owned 系统代理租约并停止内核。无服务模式 Job 可随宿主结束内核，但不能即时恢复系统代理，P2 须下次启动核对与手工修复。当前 P0 shell 不启动内核、不写代理，尚无代理功能。
