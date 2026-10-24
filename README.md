# QuickClash

仓库目录名统一为 `quick-clash`，本机目标工作路径为 `D:/repos/quick-clash`；应用显示名为 `QuickClash`，CMake project/程序基础名/命名空间使用 `quickclash`。本轮已观测实际目录迁移完成，状态及迁移后的构建处理见 [迁移记录](docs/progress/reports/20261024_repository_rename.md)。

基于 C++20 / Qt Quick 的 Mihomo 桌面客户端，内核使用 MetaCubeX 官方预编译程序。P0为9/9项Done、P1为6/6项Done，G0/G1 Pass。按用户要求采用功能开发优先、必要构建/受影响检查和单轮资源短测，见 [范围调整报告](docs/progress/reports/20261024_development_first.md)。已有Windows构建/7项测试、原型行为及双主题六场景专用工作集49.7—54.9 MB的证据满足修订后的G1；长时/帧/GPU专项未测，后置且不再阻塞功能开发。跨平台原生与CI执行转P4，订阅—代理MVP属于尚未实施的P2。

- [设计文档与范围](docs/design/README.md)：模块、UI、配置/订阅/内核/平台代理、恢复与阶段计划。
- [开发进度入口](docs/progress/README.md)：任务台账、G0—G5 清单、16 项 MVP 结果、报告与接续日志。
- [任务台账](docs/progress/TASKS.md) / [验收 checklist](docs/progress/CHECKLISTS.md)：当前任务状态、剩余步骤、阶段门与实际验收结果。
- [P0 实施报告](docs/progress/reports/p0_implementation.md) / [构建说明](docs/baseline/build_and_package.md)：已有代码、验证证据、构建限制与接续命令。
- [C++ 与源码规范](docs/development/cpp_style.md)：现代 C++20/Qt 实践，自有源码目录/文件小写下划线，类型与磁盘文件名分开。

先执行 P0 工程/工具链/官方资产/API 验证，再完成 P1 UI 与资源原型，随后交付 Windows 订阅—代理 MVP。其他桌面平台分期落地；移动只预留边界。Windows 客户端专用工作集目标≤100 MB，总工作集/私有提交另记，内核单列，必须以真实运行验收。

仓库根目录可用的文档与进度检查：

```powershell
pwsh -NoProfile -File scripts/build_design_docs.ps1 -Check
pwsh -NoProfile -File scripts/check_progress.ps1
pwsh -NoProfile -File scripts/check_source_layout.ps1
git diff --check
```

这些脚本检查文档/台账/命名，不替代实际构建、测试或性能检查。具体工具链、构建命令和支持矩阵见 P0 基线；任务数量不表示产品完成百分比。
