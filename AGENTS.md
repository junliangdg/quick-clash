# QuickClash 开发约定

- 应用显示名 QuickClash；内部 project、程序基础名及命名空间根 quickclash。
- 仓库与 checkout 目录名约定 quick-clash，目标工作路径 D:/repos/quick-clash；checkout已迁移至该路径；移动仓库后使用新的构建目录，缓存按摘要复核。源码子目录/文件仍遵循下述小写下划线约定。
- 先阅读 docs/progress/README.md、TASKS.md、CHECKLISTS.md 与 DEVLOG.md 的最新记录，按设计和依赖选择可验收任务，保留已有工作区改动。
- 遵循 docs/development/cpp_style.md：C++20、RAII、明确 QObject/资源所有权和异步生命周期；自有源码目录/文件 ASCII 小写下划线，类型名/Qt API 和工具固定名依规范例外。
- QML 文件同样小写下划线，CMake 显式 QT_QML_SOURCE_TYPENAME 导出类型；不绕过用户命名规则创建大写组件文件。
- docs/design 只维护目标、架构、技术契约、设计决策及验收标准，不追加开发流水账、实现批次、当前任务状态或实测结果；过程写 DEVLOG，实际状态写 TASKS/CHECKLISTS。reports 仅保留阶段验收、重要调查、发行核验和有价值的性能对比；同步设计是更新稳定方案，不是复制开发报告。
- 每个有意义开发交付同步受影响的任务状态/负责人、真实验收结果及 DEVLOG 下一步。日常改动不生成 report，也不把历史输出另写成 Markdown；必要的改动、验证命令、结果和限制在 DEVLOG 简记，任务证据可引用具体条目或 Actions 运行。阶段门/MVP 验收仍须逐项结论摘要，优先更新已有报告。未运行的测试不得标 Pass，相关旧证据失效标 Stale。不为记进度自动提交或创建远端事项。
- 每完成一项开发任务或独立改动，先同步受影响的设计/使用说明、TASKS、CHECKLISTS 与 DEVLOG；有相关专项报告时更新其结论，再继续下一项。实现已完成而验证未完成时明确写“验证中/NotRun”，验证或修复产生新结果后再次同步；仅更新相关记录，不强制每次改动修改全部文档。
- 原始日志、JUnit/CSV、批量截图、临时夹具及工作区快照写入 Git 忽略的 build/ 或 CI artifacts（默认14天），不提交到 reports。长期证据仅人工精选到 reports/evidence/，记录来源/版本/限制；保留重要失败结论及前后对照，不要求永久保留每次输出。过期或重复报告可合并并修复引用，当前未关闭问题与不可替代的验收证据须保留。
- 功能开发优先：代码交付做一次 Windows 增量构建和受影响的功能/现有测试；文档改动不重跑产品构建。资源敏感修改选单个10—30秒短测，有效证据可复用；长时/多轮/压力/帧/GPU专项按需后置，不为其阻塞下一功能。当前验收范围以设计及TASKS/CHECKLISTS为准。
- 改设计后运行 scripts/build_design_docs.ps1，再运行同脚本 -Check；交付前运行 scripts/check_progress.ps1、scripts/check_source_layout.ps1 与相关构建/测试。文档/台账脚本通过不等于产品验收。
- Mihomo 只用官方预编译资产，版本/来源/摘要冻结后接入；不改造内核。客户端以小内存占用为持续设计目标，不设跨机器统一的100 MB硬门槛；保持Qt Quick默认RHI/GPU渲染，必要辅助进程计入客户端统计，内核单列；专用工作集、总工作集和私有提交同时记录，同环境比较并检查缓存有界与生命周期。首版优先 Windows 本地验收，跨平台原生构建与 CI 执行后置 P4。
