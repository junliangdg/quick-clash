# MVP 配置夹具

P2-21—23新增mainstream_valid.yaml：公开虚构Hysteria2/Trojan/VLESS Reality/WS/VMess gRPC/Trojan WS与嵌套select/url-test/fallback。默认runtime测试在临时副本中将健康URL替换为自有回环HTTP 204站点，REJECT排首，断言Mihomo自动转到DIRECT/可用嵌套组，不查询私有订阅或使用公网协议服务。compatibility_unsupported.yaml覆盖协议/传输/组/RULE-SET/未知DNS模式/外部资源/未知字段；私有样式字符串为刻意公开哑值，断言不进入诊断。预检另测端口/UUID/TLS/Reality、循环/缺成员、身份与预览、32项上限；官方加载和选择不证明协议真实远端握手。

tests/fixtures/mvp不包含真实订阅/凭据。inline_valid.yaml为基础公开夹具，invalid.yaml为语法反例，provider_unsupported仍为策略反例。P3-10以geo_supported.yaml和dns_supported.yaml取代旧Geo/fake-ip拒绝夹具，覆盖固定Geo、内部fake-ip DNS、端口跳跃别名及DNS列表边界，官方隔离-t与Ready启动另测。compatibility_unsupported保留RULE-SET、未知DNS模式等实际未支持项。保留域名不可作为真实节点转发证据。

显式runtime_test --subscription-e2e从stdin读取私有URL，使用临时JsonStore和随机回环端口，先走生产订阅/选择/启动路径，再选GLOBAL实际节点并用默认TLS校验下载固定大小HTTPS响应，同时核对Mihomo连接链包含所选节点且无DIRECT。仅输出计数/状态/协议与布尔值；不作为默认CTest联网测试，不输出原始snapshot/API响应/错误详情。测试不更改系统代理，私有原文留仓库外；成功不代表所有候选节点或完整规则/DNS矩阵通过。

配置预检应先执行字节/深度/事件/重复键/类型和字段限制，再按应用偏好合成无 secret 语义模板，最后生成认证 SessionConfig、独立目录内执行官方核心 -t。单元夹具通过后也不能直接证明转发可用。

P2-13已实现tests/integration/network_fixture.*：独立HTTP CONNECT代理与TLS站点使用随机回环端口，GET固定响应与域名CONNECT目标同时断言。保留域名fixture.quickclash.invalid在CONNECT fixture内映射至TLS站点，因此不能直连DNS成功来掩盖代理路径。客户端明确使用本地mixed入口，产品网站测试另用HEAD。记录方法/去查询串的路径/CONNECT目标，不包含真实订阅、密钥或用户代理快照。该逻辑远端位于同一开发机，未宣称公网跨机验收。

P2-14使用独立WinINet PRECONFIG测试客户端验证系统代理、重复开启、端口迁移和停止后原快照相等；默认CTest不改OS设置，显式--native-system-proxy才运行该专项。tests/fixtures/network的公开证书/私钥仅用于本地测试，Qt信任限测试进程，WinINet未知CA例外限该请求，不改OS信任库。产品TLS策略无绕过。当前结果见 [P2报告](../progress/reports/20261024_p2_implementation.md)。

P2-20的TLS夹具按User-Agent协商YAML并刻意返回text/html，断言默认UA/Accept、首尾空白、显式请求头覆盖。HTML/Base64/空更新均保留旧源和Ready会话。真实订阅仅经本地标准输入临时验证，仓库不保存URL、token、正文、节点名或凭据，证据仅匿名S1—S4结构计数。
