# 任务与验收证据

本页仅作报告索引与证据规范，不重复开发流水或维护第二份任务状态。当前状态以[任务台账](../TASKS.md)和[验收清单](../CHECKLISTS.md)为准，修改过程见[开发日志](../DEVLOG.md)。

## 专项报告

| 主题 | 报告 |
|---|---|
| 订阅更新后内存异常调查 | [调查与对照证据](20261026_subscription_memory_investigation.md) |
| 客户端内存优化及最终跨批次审查 | [持续报告与验证矩阵](20261026_client_memory_optimization.md) |
| 系统代理冲突显式开启重建基线 | [修复报告](20261026_system_proxy_conflict_fix.md) |
| 统一视觉规范与Light/Dark概念预览 | [当前设计与验证](20261027_unified_visual_design.md) |
| 依赖获取与Geo固定来源 | [工具维护报告](20261026_dependency_fetch_fix.md) |
| 代理出口与订阅更新内存 | [功能修复报告](20261025_proxy_memory_fixes.md) |
| Windows MVP范围与日常交付路线 | [G2范围复核](20261025_g2_scope_review.md)、[G3方案](20261025_g3_windows_plan.md) |

## 阶段与历史证据

历史报告只证明其记录版本和条件；不从旧报告继承当前产品Pass或资源结论。

| 主题 | 报告 |
|---|---|
| P0/P1工程与原型 | [实施基线](p0_implementation.md)、[Windows验证](20261024_p0_p1_validation.md)、[资源证据](20261024_windows_private_working_set.md)、[G1范围](20261024_development_first.md) |
| P2功能与界面 | [功能交付](20261024_p2_implementation.md)、[响应式/语言/链接](20261024_p2_responsive_localization.md)、[协议交付](20261024_p2_protocols.md)、[窗口缩放](20261024_resize_layout.md) |
| 订阅、日常链路与恢复 | [订阅闭环](20261024_p3_subscription.md)、[日常链路](20261025_daily_use.md)、[恢复与包](20261025_g2_recovery.md)、[功能计划](20261024_mihomo_feature_plan.md) |
| 早期构建与目录迁移 | [P0接续](20261024_p0_validation.md)、[P1落地](20261024_p1_applied.md)、[P1补丁](20261024_p1_implementation.md)、[AutoUic](20261024_autouic_fix.md)、[QML缓存警告](20261024_qmlcache_warning_fix.md)、[目录迁移](20261024_repository_rename.md) |

## 报告与证据规范

- 使用[任务报告模板](../templates/task-report.md)或[验收报告模板](../templates/acceptance-report.md)，填写真实事实；模板不能直接当完成证据。
- 报告标识源码/补丁、构建、Qt/Mihomo/OS版本、输入摘要、真实命令/退出码、未运行项与限制；一次运行可以支撑多个验收ID，但需分别列出条件和结果。
- 本地证据路径以报告目录为基准，链接必须实际存在。大产物可存构建设施，但应记录不可变版本、摘要与获取位置；不为消除断链伪造产物。
- 新结果保留旧失败和旧版本记录，通过任务/清单维护当前状态，不在索引追加逐次修改摘要。
- 禁止提交真实订阅、节点凭据、控制秘密、非公开私钥或未脱敏用户网络快照；[公开网络夹具](../../../tests/fixtures/network/readme.md)仅供测试，不能用于真实服务。
