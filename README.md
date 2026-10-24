# QuickClash

仓库目录名统一为 `quick-clash`，本机目标工作路径为 `D:/repos/quick-clash`；应用显示名为 `QuickClash`，CMake project/程序基础名/命名空间使用 `quickclash`。本轮已观测实际目录迁移完成，状态及迁移后的构建处理见 [迁移记录](docs/progress/reports/20261024_repository_rename.md)。

基于C++20/Qt Quick的Mihomo客户端，内核使用官方预编译程序。P0/P1完成，Windows P2已有订阅—官方内核—系统代理链路、窄窗三底栏、自动中英文及[主流协议/自动组/兼容性诊断](docs/progress/reports/20261024_p2_protocols.md)。本轮修复连续缩放时D3D11等待阻塞及导航布局重复协商，见[缩放修复](docs/progress/reports/20261024_resize_layout.md)。当前本地包dist/p2_resize/bin/quickclash.exe，需保留完整目录并退出旧版本后启动，使用方式见[Windows指南](docs/baseline/windows_user_guide.md)。P2为20Done/2Review/2Blocked，G2仍Blocked；真实Windows事件、关键恢复和干净普通用户环境尚待核对。Geo/fake-ip DNS仍按P3分期，新增协议的公网握手未测，不能把链接识别或官方内核加载当作全部订阅可用。跨平台/CI后置P4，只做必要检查与单轮短测。

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
