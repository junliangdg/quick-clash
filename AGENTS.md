# QuickClash 开发约定

- 应用显示名 QuickClash；内部 project、程序基础名及命名空间根 quickclash。
- 仓库与 checkout 目录名约定 quick-clash，目标工作路径 D:/repos/quick-clash；checkout已迁移至该路径；移动仓库后使用新的构建目录，缓存按摘要复核。源码子目录/文件仍遵循下述小写下划线约定。
- 先阅读 docs/progress/README.md 与 TASKS.md，按需查看 CHECKLISTS.md、DEVLOG.md 和相关设计；按实际依赖选择任务，保留已有工作区改动。
- 遵循 docs/development/cpp_style.md：C++20、RAII、明确 QObject/资源所有权和异步生命周期；自有源码目录/文件 ASCII 小写下划线，类型名/Qt API 和工具固定名依规范例外。
- QML 文件同样小写下划线，CMake 显式 QT_QML_SOURCE_TYPENAME 导出类型；不绕过用户命名规则创建大写组件文件。
- docs/design 只维护目标、架构、技术契约、设计决策及验收标准，不追加开发流水账或当前实测结果；当前任务和风险写 TASKS，发布核验写 CHECKLISTS，必要过程写 DEVLOG。同步设计是更新稳定方案，不是复制开发报告。
- 一次交付或会话结束时更新受影响的当前状态、验证结果和下一步；简单改动在 TASKS 一处说明清楚即可，不强制重复写 DEVLOG。重要失败、交接上下文及复杂验证按需写 DEVLOG，报告仅用于重要调查和发布核验。不强制中途逐项同步或填写单人负责人；未验证的实现保持进行中，未运行的测试不得标 Pass，旧证据变化时明确适用范围或 Stale。不为记进度自动提交或创建远端事项。
- 原始日志、JUnit/CSV、批量截图、临时夹具及工作区快照写入 Git 忽略的 build/ 或 CI artifacts（默认14天），不提交到 reports。长期证据仅人工精选到 reports/evidence/，记录来源/版本/限制；保留重要失败结论及前后对照，不要求永久保留每次输出。过期或重复报告可合并并修复引用，当前未关闭问题与不可替代的验收证据须保留。
- 功能开发优先：代码交付做一次 Windows 增量构建，并按风险运行受影响的测试或针对性人工检查；文档改动不重跑产品构建。配置/订阅/持久化验证成功路径和失败保留数据；系统代理、TUN、权限及更新增加适用的恢复、退出和失败路径。资源敏感改动按需选择可比较短测，长时/多轮/压力/帧/GPU专项按风险安排。发布前按实际声明范围检查 CHECKLISTS；历史阶段门不机械阻塞独立能力。
- 改设计后运行 scripts/build_design_docs.ps1，再运行同脚本 -Check；交付前运行 scripts/check_progress.ps1、scripts/check_source_layout.ps1 与相关构建/测试。文档/台账脚本通过不等于产品验收。
- Mihomo 只用官方预编译资产，版本/来源/摘要冻结后接入；不改造内核。客户端以小内存占用为持续设计目标，不设跨机器统一的100 MB硬门槛；保持Qt Quick默认RHI/GPU渲染，必要辅助进程计入客户端统计，内核单列；专用工作集、总工作集和私有提交同时记录，同环境比较并检查缓存有界与生命周期。首版优先 Windows 本地验收，跨平台原生构建与 CI 执行后置 P4。
