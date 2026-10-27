# QuickClash

仓库目录名统一为 `quick-clash`，本机目标工作路径为 `D:/repos/quick-clash`；应用显示名为 `QuickClash`，CMake project/程序基础名/命名空间使用 `quickclash`。本轮已观测实际目录迁移完成，状态及迁移后的构建处理见 [迁移记录](docs/progress/reports/20261024_repository_rename.md)。

基于C++20/Qt Quick的Mihomo客户端，使用官方预编译内核。P0/P1完成，Windows已有订阅—官方内核—系统代理真实链路、窄窗三底栏、中英文及主流协议/自动组支持，当前本机包为dist/g2_recovery/bin/quickclash.exe，需保留完整目录并退出旧版本后启动，见[Windows指南](docs/baseline/windows_user_guide.md)。按设计1.9的[G2范围复核](docs/progress/reports/20261025_g2_scope_review.md)，G2聚焦本机最小闭环，P2现21Done/2Review/1Todo，G2 NotRun待证据汇总；干净普通用户环境与有限真实OS/GUI宿主补证归P3-14/G3。完整Windows日常功能仍在开发，未宣称可交付。当前运行事实见[恢复报告](docs/progress/reports/20261025_g2_recovery.md)，跨平台原生/CI后置P4，不统一要求全协议握手或故障/性能穷举矩阵。

- [设计文档与范围](docs/design/README.md)：模块、UI、配置/订阅/内核/平台代理、恢复与阶段计划。
- [开发进度入口](docs/progress/README.md)：任务台账、G0—G5 清单、16 项 MVP 结果、报告与接续日志。
- [任务台账](docs/progress/TASKS.md) / [验收 checklist](docs/progress/CHECKLISTS.md)：当前任务状态、剩余步骤、阶段门与实际验收结果。
- [P0 实施报告](docs/progress/reports/p0_implementation.md) / [构建说明](docs/baseline/build_and_package.md)：已有代码、验证证据、构建限制与接续命令。
- [C++ 与源码规范](docs/development/cpp_style.md)：现代 C++20/Qt 实践，自有源码目录/文件小写下划线，类型与磁盘文件名分开。

先执行 P0 工程/工具链/官方资产/API 验证，再完成 P1 UI 与资源原型，随后交付 Windows 订阅—代理 MVP。其他桌面平台分期落地；移动只预留边界。客户端以小内存占用为设计目标，保持 Qt Quick 默认 RHI/GPU 渲染，不设跨机器统一的固定内存门槛。Windows 同时记录客户端进程组专用工作集、总工作集与私有提交，内核单列；以真实负载、同环境基线和有界生命周期评价资源变化。

仓库根目录可用的文档与进度检查：

```powershell
pwsh -NoProfile -File scripts/build_design_docs.ps1 -Check
pwsh -NoProfile -File scripts/check_progress.ps1
pwsh -NoProfile -File scripts/check_source_layout.ps1
git diff --check
```

这些脚本检查文档/台账/命名，不替代实际构建、测试或性能检查。具体工具链、构建命令和支持矩阵见 P0 基线；任务数量不表示产品完成百分比。
