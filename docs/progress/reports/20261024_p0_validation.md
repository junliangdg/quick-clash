# P0 接续：构建执行与内核验证证据

- 日期/执行者：2026-10-24 / Codex；设计 1.4。
- 任务：P0-04/06/07/08/09；本地 HEAD `1ee26e6` 加本次工作区补丁，保留此前进度文档修改，未提交或触发远端工作流。
- 结论：相关任务仍为 Blocked，P0 保持 4 Done / 5 Blocked，G0 不通过。完成验证基础设施及部分真实检查；未获得完整客户端构建通过记录。

## 实现与边界

| 位置 | 行为 | 尚需确认 |
|---|---|---|
| [native_process.ps1](../../../scripts/native_process.ps1)、[独立测试](../../../tests/scripts/native_process_test.ps1) | 用参数数组启动原生进程，异步排空两条输出管道，保存日志与退出码/耗时/超时元数据；超时只结束该步拥有的进程树 | 构建工具及不同平台上的实际使用由原生构建/CI 验证；这不是产品进程适配器 |
| [build_windows.ps1](../../../scripts/build_windows.ps1) | 保留 VS/Qt/Ninja 的选择方式；每次生成独立 logs 目录，配置/构建有超时，UI 自动 lint，CTest 保存 JUnit | 本次完整调用在审批阶段未执行，不能据此关闭 P0-06 |
| [CMakeLists.txt](../../../CMakeLists.txt) | headless 只显式要求 Qt Core，契约测试限时 30 秒；offscreen smoke 使用 QT_QUICK_BACKEND=software | 新 CMake 配置/链接、缓存和 CTest 尚未运行 |
| [probe_mihomo.ps1](../../../scripts/probe_mihomo.ps1) | 每次新证据目录，不覆盖历史；-t 限时 10 秒，正/反退出码和副作用断言，失败阶段与清理结果保存，secret 脱敏 | API-only 实际重跑；监听/Job 和强制网络隔离仍分别 NotRun |
| [原生 CI](../../../.github/workflows/build.yml) | 增加执行器测试；Windows 调用构建入口和完整官方内核探针；保留 UI/headless/核心证据，job 限时 25 分钟 | 尚未远端执行，不宣称三平台支持已通过 |

以上改动属于 P0 工程与技术验证，不接入系统代理或订阅业务。客户端总内存与 UI 性能没有测量；执行器位于开发脚本，不加入客户端常驻进程。

## 实际验证

| 命令/检查 | 实际结果 | 证据/限制 |
|---|---|---|
| build_windows.ps1，Qt 6.8.0、显式 Ninja、独立 build/p0_windows -Fresh | 未执行 | 自动审批服务 401 Unauthorized / 无可用账户凭据，无法完成审批；不是源码编译失败或安全性判定，未绕过审批改用其他构建通道 |
| pwsh -NoProfile -File tests/scripts/native_process_test.ps1 | 退出 0 | 中文/空格/引号/字面变量参数、大量 stdout/stderr、退出 7、3 秒超时以及拥有的子/孙进程均已结束；测试临时目录清理 |
| Qt 6.8.0 qmllint，两份 QML，导入已有 build/dev 和 Qt qml 目录 | 退出 0，无警告输出 | 使用现有生成模块验证源码，不能代替本次 CMake 生成/cache/runtime |
| fetch_dependencies.ps1 -Offline | 退出 0 | yaml-cpp 0.9.0 与官方 Mihomo v1.19.33 摘要一致 |
| probe_mihomo.ps1 -ApiOnly | 三次递进版本均退出 0 | 最终补丁版本见下方第三次证据；无系统代理修改或外部节点请求 |
| PowerShell Parser.ParseFile，scripts 与 tests/scripts | 无解析错误 | 首次检查命令被外层 shell 展开导致 ParserError，改用当前 shell 直接解析后通过；不是产品脚本语法错误 |
| clang-format --dry-run --Werror，自有 C++ | 退出 0 | 本次未改 C++；真实 clang-tidy/契约测试仍待编译 |
| check_progress.ps1、check_source_layout.ps1、build_design_docs.ps1 -Check、git diff --check | 均退出 0 | P0=4 Done/5 Blocked，G0=Blocked，MVP=0/16；源码命名、19 分册及合并版/链接一致 |
| cmake --list-presets、保存的 API payload secret/清理记录核对 | 退出 0 | dev/headless preset 可解析；报告内 payload 的 secret 已替换，最终会话清理有记录 |

探针重跑记录保留，避免用新结果覆盖旧运行：

- 第一次：[summary](p0_core/20261024_api_recheck/summary.json)、[API](p0_core/20261024_api_recheck/api.json)。
- 第二次（增加 scope/进程清理字段）：[summary](p0_core/20261024_api_final/summary.json)、[API](p0_core/20261024_api_final/api.json)。
- 第三次（当前版本，记录临时会话清理）：[summary](p0_core/20261024_api_03/summary.json)、[API](p0_core/20261024_api_03/api.json)、[正例日志](p0_core/20261024_api_03/validation.log)、[反例日志](p0_core/20261024_api_03/invalid.log)。

第三次确认正例 -t 退出 0、反例退出 1、正例未新增文件、错误认证 401、带保留字符组名编码/选择读回、三模式读回、重载和重启选择重置。结束时 owned 进程退出、会话目录删除；API 请求中 secret 替换为 redacted。证据明确保留 listeners/job/network_isolation=NotRun，不把控制面通过等同于真实转发。

## 接续

完整构建审批仍是 P0-06 的阻塞。已发现原有 build/dev/quickclash.exe，但缺少本次可追溯完整构建/测试记录，保留它且不据此标 Done。恢复审批服务后使用构建基线的独立目录命令，保存本次脚本的日志；随后运行 headless、完整 probe、部署命令，并取得 Linux/macOS 原生 CI 证据。G0-B 的强制无网络隔离仍需独立测试方案和证据，不由本次探针自动提供。
