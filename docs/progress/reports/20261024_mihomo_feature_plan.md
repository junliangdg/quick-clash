# Mihomo主流协议与功能分期

2026-10-24后续状态：P2-21/22/23已实现并完成本次范围检查，见[协议交付](20261024_p2_protocols.md)；下文Todo/未实现为制定分期时的历史事实。Geo/DNS/高级能力仍未实现。

日期/执行者：2026-10-24 / Codex。根据用户要求，将标准订阅链接适配与配置能力分开跟踪。本文是新增开发任务的范围和验收约定，不表示协议或DNS功能已实现。官方Mihomo v1.19.33预编译程序保持不变，客户端负责配置分类、受管覆盖、API展示和恢复。

## 分期与完成条件

| 阶段/任务 | 功能 | 实现边界与必要验收 |
|---|---|---|
| P2-20，已交付 | 标准HTTPS订阅链接自动适配 | 默认Clash/Mihomo UA与YAML Accept；显式头覆盖；内容识别与冻结配置预检分开；不转换Base64/分享链接 |
| P2-21，Todo | Hysteria2、Trojan、VLESS、VMess基础协议 | 保留现有HTTP/SOCKS5/Shadowsocks；逐协议定义字段类型、必填项、上下界及秘密脱敏，支持常用TLS/SNI/ALPN/UDP、VLESS Reality/flow和所需TCP/WS/gRPC传输字段。固定官方内核校验接受，导入/选择/启动/坏候选保留路径可用，协议凭据变化使旧身份失效；内核校验不等于真实远端转发通过 |
| P2-22，Todo | url-test、fallback代理组 | 支持url/interval/tolerance/lazy/timeout等冻结字段及嵌套引用检查；自动组展示实际类型、当前节点和成员，选择操作按内核能力启用。健康检查交Mihomo，客户端不重复轮询/并发测速；自动组及嵌套select可用，错误/更新保留旧确认状态 |
| P2-23，Todo | 可解释配置兼容性诊断 | 将笼统UnsupportedConfigFeature细分为不支持协议/传输/组/规则/DNS/外部资源；提示安全字段路径、功能名及阶段，不回显URL、节点名、凭据或原始YAML。无需逐个修复才发现下一项；中英文共用错误码 |
| P3-02，现有Todo扩充 | GEOIP、GEOSITE、RULE-SET及受管资源 | 锁定官方兼容Geo资产来源、版本、摘要、许可与文件名，离线校验/运行使用同一版本；禁用配置提供的任意下载地址。资源缺失/损坏明确失败，原资源保留，更新可回退；以受控匹配断言验证规则，不用DIRECT可达性冒充Geo命中 |
| P3-08，Todo | fake-ip DNS与普通DNS管理 | 独立于TUN：支持fake-ip/redir-host、默认解析/上游/引导解析、过滤与缓存边界；默认只回环监听、端口冲突检查、代理域名引导与解析回环防护。显式开启才生效；受控DNS查询/规则匹配/失败回退通过；普通系统代理模式不暗改系统DNS，DNS劫持/TUN联动依赖P3-04 |
| P3-09，Todo | 高级协议/传输与组策略扩展 | 按锁定内核逐项评估TUIC、Hysteria旧版、WireGuard/SSH、load-balance及更复杂传输/客户端指纹。列明支持矩阵和外部证书/密钥管理，逐类落地；不承诺任意字段透传，不为追求数量一次放开全部内核配置 |

P2-21优先实现三种真实样本所需的Hysteria2/Trojan/VLESS，再补VMess；单个协议可分别记录代码和证据，但整项未完成前不标Done。P2-22与P2-23在基础协议后接续。P2-17/G2增加对三项的依赖；P3能力不人为前置到G2，私有样本也不充当唯一验收夹具。新增功能沿用一次构建、受影响检查、必要单轮短测，不新增长期性能矩阵。

## 当前兼容性事实与后续路径

四份用户授权的本地私有样本均返回HTTP 200与Clash YAML；三份MIME误标text/html，已由内容识别处理。匿名结构检查见[脱敏统计](p2_evidence/20261024/responsive/subscription_samples.json)。S1/S4包含Hysteria2/Trojan、url-test/fallback、GEOIP和fake-ip；S2包含VLESS、url-test、GEOIP/GEOSITE；S3包含Hysteria2/VLESS、url-test/fallback、GEOIP和fake-ip。因此当前配置预检明确拒绝，不能报告这些地址已经导入或完成公网节点转发。

P2协议/组完成后，仍含GEOIP/GEOSITE或fake-ip的原始配置须等对应P3任务；可用公开、无Geo/fake-ip依赖的脱敏夹具验证P2，不静默删规则或关闭必要DNS来假称原订阅生效。需要覆盖其余Mihomo功能时先补分类与任务，再更新配置策略和能力说明。

真实订阅地址、查询token、路径凭据、正文和节点信息均只供本地临时测试，不进入仓库、任务正文或公开夹具。后续复测由本地标准输入提供地址；仓库只保存匿名统计和稳定结果码。

本次仅新增任务与修订设计；未执行这些新协议/Geo/DNS功能验收，状态均Todo/NotRun。设计生成与台账检查记录在[本轮交付报告](20261024_p2_responsive_localization.md)。
