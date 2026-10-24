# MVP 配置夹具

P2-21—23新增mainstream_valid.yaml：公开虚构Hysteria2/Trojan/VLESS Reality/WS/VMess gRPC/Trojan WS与嵌套select/url-test/fallback。runtime测试在临时副本中将健康URL替换为自有回环HTTP 204站点，REJECT排首，断言Mihomo自动转到DIRECT/可用嵌套组，不查询私有订阅或使用公网协议服务。compatibility_unsupported.yaml同时覆盖协议/传输/组/Geo/DNS/外部资源/未知字段；私有样式字符串为刻意公开的哑值，断言不进入诊断。预检另测端口/UUID/TLS/Reality、循环/缺成员、身份与预览、32项上限；官方加载和选择不证明新协议真实远端握手。

tests/fixtures/mvp 不包含真实订阅/凭据。inline_valid.yaml 含内嵌 HTTP 节点、选择组、域名/IP/MATCH；example.invalid 为保留域名，节点不可转发，不把控制面测试当远端流量证据。invalid.yaml 是语法反例；provider_unsupported、geo_unsupported、fake_ip_unsupported 为可解析但应被应用拒绝的策略反例。

配置预检应先执行字节/深度/事件/重复键/类型和字段限制，再按应用偏好合成无 secret 语义模板，最后生成认证 SessionConfig、独立目录内执行官方核心 -t。单元夹具通过后也不能直接证明转发可用。

P2-13已实现tests/integration/network_fixture.*：独立HTTP CONNECT代理与TLS站点使用随机回环端口，GET固定响应与域名CONNECT目标同时断言。保留域名fixture.quickclash.invalid在CONNECT fixture内映射至TLS站点，因此不能直连DNS成功来掩盖代理路径。客户端明确使用本地mixed入口，产品网站测试另用HEAD。记录方法/去查询串的路径/CONNECT目标，不包含真实订阅、密钥或用户代理快照。该逻辑远端位于同一开发机，未宣称公网跨机验收。

P2-14使用独立WinINet PRECONFIG测试客户端验证系统代理、重复开启、端口迁移和停止后原快照相等；默认CTest不改OS设置，显式--native-system-proxy才运行该专项。tests/fixtures/network的公开证书/私钥仅用于本地测试，Qt信任限测试进程，WinINet未知CA例外限该请求，不改OS信任库。产品TLS策略无绕过。当前结果见 [P2报告](../progress/reports/20261024_p2_implementation.md)。

P2-20的TLS夹具按User-Agent协商YAML并刻意返回text/html，断言默认UA/Accept、首尾空白、显式请求头覆盖。HTML/Base64/空更新均保留旧源和Ready会话。真实订阅仅经本地标准输入临时验证，仓库不保存URL、token、正文、节点名或凭据，证据仅匿名S1—S4结构计数。
