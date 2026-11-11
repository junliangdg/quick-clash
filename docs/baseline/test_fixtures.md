# MVP 配置夹具

`tests/fixtures/mvp`只含公开虚构配置，不包含真实订阅或凭据。现行输入与资源边界以[07](../design/07-订阅与资源更新.md#11-配置支持边界)为准；夹具名称和早期P2/P3编号不构成客户端协议白名单。测试入口及自动/人工范围见[验收映射](../design/16-设计决策风险与需求追踪.md#31-关键验收的验证入口)。

`inline_valid.yaml`为基础配置，`invalid.yaml`为语法反例。`mainstream_valid.yaml`和`advanced_supported.yaml`包含Hysteria2/Trojan/VLESS/VMess/TUIC、传输选项和嵌套或自动策略组；`geo_supported.yaml`、`dns_supported.yaml`覆盖受管Geo与DNS。`contracts`检查结构、预算、受管字段、身份和预览脱敏；协议、传输、策略组、规则及DNS具体取值交给官方内核校验。`runtime`在临时副本中将健康URL替换为回环HTTP 204站点，检查自动选择和内核读回，不查询私有订阅或公网协议服务。

`provider_unsupported.yaml`验证原始provider依赖不能绕过受管下载/快照管线直接进入预检，不表示产品拒绝provider。`compatibility_unsupported.yaml`是交给官方内核拒绝的合成坏配置，其私有样式字符串均为公开哑值；未知字段或`RULE-SET`本身不因此被一概拒绝。`core_compatible.yaml`及`runtime_core_compatibility`另覆盖官方内核配置、JSON、原生provider、URI/Base64导入、坏正文保留确认版本及离线备份预览。官方加载、Ready和组成员读回不能证明各协议的真实远端握手。

显式runtime_test --subscription-e2e从stdin读取私有URL，使用临时JsonStore和随机回环端口，先走生产订阅/选择/启动路径，再选GLOBAL实际节点并用默认TLS校验下载固定大小HTTPS响应，同时核对Mihomo连接链包含所选节点且无DIRECT。仅输出计数/状态/协议与布尔值；不作为默认CTest联网测试，不输出原始snapshot/API响应/错误详情。测试不更改系统代理，私有原文留仓库外；成功不代表所有候选节点或完整规则/DNS矩阵通过。

配置预检先执行字节/深度/事件/重复键/类型、受管字段与资源限制，再按应用偏好合成无secret语义模板；会话配置、官方`-t`和原生provider正文验证遵循[配置事务](../design/06-配置管理与事务应用.md)及07的契约。独立进程和目录不构成OS权限沙箱，单元夹具通过也不能证明转发可用。

P2-13已实现tests/integration/network_fixture.*：独立HTTP CONNECT代理与TLS站点使用随机回环端口，GET固定响应与域名CONNECT目标同时断言。保留域名fixture.quickclash.invalid在CONNECT fixture内映射至TLS站点，因此不能直连DNS成功来掩盖代理路径。客户端明确使用本地mixed入口，产品网站测试另用HEAD。记录方法/去查询串的路径/CONNECT目标，不包含真实订阅、密钥或用户代理快照。该逻辑远端位于同一开发机，未宣称公网跨机验收。

P2-14使用独立WinINet PRECONFIG测试客户端验证系统代理、重复开启、端口迁移和停止后原快照相等；默认CTest不改OS设置，显式--native-system-proxy才运行该专项。tests/fixtures/network的公开证书/私钥仅用于本地测试，Qt信任限测试进程，WinINet未知CA例外限该请求，不改OS信任库。产品TLS策略无绕过。当前结果见 [P2报告](../progress/reports/windows_validation.md#订阅与代理功能)。

TLS夹具按User-Agent协商YAML并刻意返回text/html，检查默认UA/Accept、首尾空白、显式请求头覆盖和按内容识别。HTML、空内容及官方内核拒绝的URI/Base64正文须保留旧源和Ready会话；合法URI/Base64由兼容探针验证成功路径。真实订阅仅经本地标准输入临时验证，仓库不保存URL、token、正文、节点名或凭据，历史证据使用匿名结构计数。
