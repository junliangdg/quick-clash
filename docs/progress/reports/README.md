# 关键报告与证据

日常开发改动不生成报告或复制历史日志；当前任务、结果和风险以[TASKS](../TASKS.md)为入口，发布核验使用[CHECKLISTS](../CHECKLISTS.md)。重要失败、复杂验证及接续信息按需写[DEVLOG](../DEVLOG.md)，有效CI记录可直接链接，不重复抄写。

| 保留主题 | 用途 |
|---|---|
| [Windows阶段验证](windows_validation.md) | 工程/原型、订阅转发/恢复、阶段范围与关键结论 |
| [真实内存调查](20261026_subscription_memory_investigation.md) | 未关闭字体/私有驻留异常及重要对照 |
| [内存优化验证范围](20261026_client_memory_optimization.md) | 实现上界、历史静态检查与后续运行证据 |
| [产品视觉验收](20261027_unified_visual_implementation.md) | 统一视觉/双主题/八页面实际验证与代表截图 |
| [CI与持续交付](20261027_github_release.md) | 包内容、CI故障修复及真实发布待验范围 |

## 留存规则

- 仅重要未关闭调查、发行核验或不可替代性能对比需要长期报告；优先更新对应摘要，不为开发/重试新增Markdown。历史阶段/MVP证据保留原条件、结论与适用版本，模板不是证据；新任务不强制报告或逐项链接。
- 原始日志、JUnit/CSV、批量JSON/截图、临时PID/夹具和工作区清单写Git忽略的build/；CI artifacts默认14天，Release资产独立保留。重要结果到期前摘出关键数据，过期产物不作为当前可获取证据。
- evidence/仅放人工精选且报告引用的长期证据，.gitignore列出允许文件；来源/SHA见[清单](evidence/evidence_manifest.json)。原文件结果不改写；新证据须有不可替代价值并更新摘要/清单/允许列表，不搬整个运行目录。
- 保留重要失败、前后结论及风险，无需永久保存每次输出。过期实施/交接报告合并后移除并修复引用，Git历史可查。清理前内容临时复制到本机build/report_archive/（忽略），不作为共享证据或长期Markdown台账。
- 旧数据只证明原构建/条件，精简不把NotRun/Fail/Stale转Pass。禁止真实订阅、凭据/秘密或未脱敏网络快照入库；公开夹具见[说明](../../../tests/fixtures/network/readme.md)。
