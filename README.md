# QuickClash

基于 C++20 / Qt Quick 的 Mihomo 桌面客户端。应用名 **QuickClash**，内部程序/命名空间名 `quickclash`；内核使用 MetaCubeX 官方预编译 Mihomo。当前处于设计与开发准备阶段，尚无可运行客户端，不宣称功能或资源目标已验证。

- [设计文档与范围](docs/design/README.md)：模块、UI、配置/订阅/内核/平台代理、恢复与阶段计划。
- [开发进度入口](docs/progress/README.md)：任务台账、G0—G5 清单、16 项 MVP 结果、报告与接续日志。
- [C++ 与源码规范](docs/development/cpp_style.md)：现代 C++20/Qt 实践，自有源码目录/文件小写下划线，类型与磁盘文件名分开。

先执行 P0 工程/工具链/官方资产/API 验证，再完成 P1 UI 与资源原型，随后交付 Windows 订阅—代理 MVP。其他桌面平台分期落地；移动只预留边界。客户端总内存目标≤100 MB，内核单列，必须以真实运行验收。

仓库根目录可用的准备检查：

```powershell
pwsh -NoProfile -File scripts/build_design_docs.ps1 -Check
pwsh -NoProfile -File scripts/check_progress.ps1
pwsh -NoProfile -File scripts/check_source_layout.ps1
git diff --check
```

这些脚本检查文档/台账/命名，不替代尚待实现的构建、测试或性能检查。具体工具链、构建命令和支持矩阵由 P0 记录。
