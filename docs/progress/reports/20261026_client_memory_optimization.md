# 客户端内存优化：实现与验证范围

2026-10-26 / Zoo。保留实现边界及未关闭风险，移除工作区计数、格式日志及交接流水。优化P0—P3是优先级，不代表仓库阶段完成；当前状态以[TASKS](../TASKS.md)/[CHECKLISTS](../CHECKLISTS.md)为准。

## 实现范围

| 切片 | 实现及上界 |
|---|---|
| P0 | [有界读取](../../../src/infra/io/bounded_read.h)按64KiB增量读取并保留限额/短读处理；Geo至多32MiB、流式摘要、ACL及原子提交。只读附着采样记录三指标，内核单列。 |
| P1-A | worker数据经[latest mailbox](../../../src/infra/storage/latest_mailbox.cpp)合并完整通道，控制/Operation可靠事件独立；GUI增量列表减少JSON展开，队列/基线有界。通知解锁后发，关闭与发布同锁。 |
| P1-B | 会话/需求/操作/请求联合票据防过期发布；离页清基线、重入完整重发。日志暂停/释放分开，App100条、页面1000条、服务200条及消息限额，未宣称完整通用预算已验收。 |
| P2 | provider私有暂存每项4MiB、总16MiB/32项，规则输出16MiB/20000条、扫描128MiB/200000事件；保留TLS/拒重定向、解析预算/schema/别名拒绝、官方校验和失败留旧。弱回调/代次/串行事务移交减少重叠。 |
| P3 | [稳定ID模型](../../../src/presentation/profile_model.cpp)、1—3列投影与虚拟卡片，管理弹层页面持有。隐藏/最小化5秒可选卸载默认关闭且要求真实托盘，精确定时器/引擎代次及取消保护，业务继续运行。 |

## 验证范围

2026-10-26跨批次审查时，各切片实现Review、产品验证NotRun；只运行格式/QML文本/翻译/登记及文档检查。58份C++格式、9份QML文本、189处翻译和3份PowerShell解析通过，独立PowerShell采样3项通过。进度因旧包缺失退出1，未伪造产物；汇总脚本退出0未把子项改Pass。当时没有CMake/MOC/编译/产品测试或收益测量。

| 切片 | 定向测试入口 | 原审查结论 |
|---|---|---|
| P0 | bounded_read、geo_resource_writer、storage | C++/ACL/实际Geo/产品内存NotRun |
| P1-A | latest_mailbox、runtime_publication、runtime_list_model | 并发/可靠事件/模型NotRun |
| P1-B | ui_state、runtime_ui_probe、runtime_publication | 服务IPC/撤销/隐藏恢复NotRun |
| P2 | provider_stream、runtime | YAML/官方校验/取消补偿/网络NotRun |
| P3 | profile_model、deferred_ui_unload、ui_subscriptions英中、host_lifecycle | 复用/键鼠/真实托盘/恢复/内存NotRun |

之后[CI与发行核验](20261027_github_release.md#本地验证)取得Windows全目标构建、22项回归、另3项定向及headless5项通过，属于后续证据；不改写原NotRun，也不因单元/GUI通过关闭真实字体、OS故障或长期内存问题。专项仍Review，复用有效证据核对剩余缺口。

## 页面缓存补充

同日代理/日志页绑定多次访问完整ui.runtime/nodes/groups/delays/telemetry，重复物化JS树；改为页面只读缓存、nodeByName/groupNames索引，worker日志正则static单次编译，通道/票据不变。cmake --build build/dev --target quickclash退出0，qml_smoke/ui_responsive/ui_runtime_lists 3/3及格式通过。真实切页内存收益NotRun，未宣称具体MB节省。

## 未关闭风险

- worker关闭/邮箱及迟到回调、日志服务确认、校验→提交→启动→补偿取消，需要核对运行覆盖。
- ACL、junction竞态、OS拒删/强杀残留；摘要一致不等于上游签名真实性。
- 可变行高/焦点/复用ID、旧偏好、无托盘/托盘丢失恢复；offscreen不能替代真实桌面。
- 完整JSON发布、元数据及分配器保留仍存在，释放引用不代表OS返页。[真实字体调查](20261026_subscription_memory_investigation.md)未关闭，不设固定100MB门槛。

后续只补定向缺口，资源敏感修改选一次10—30秒短测。日常改动不追加本报告流水或新建report。
