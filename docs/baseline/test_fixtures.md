# MVP 配置夹具

tests/fixtures/mvp 不包含真实订阅/凭据。inline_valid.yaml 含内嵌 HTTP 节点、选择组、域名/IP/MATCH；example.invalid 为保留域名，节点不可转发，不把控制面测试当远端流量证据。invalid.yaml 是语法反例；provider_unsupported、geo_unsupported、fake_ip_unsupported 为可解析但应被应用拒绝的策略反例。

配置预检应先执行字节/深度/事件/重复键/类型和字段限制，再按应用偏好合成无 secret 语义模板，最后生成认证 SessionConfig、独立目录内执行官方核心 -t。单元夹具通过后也不能直接证明转发可用。

受控端到端环境在 P2-13 实现：独立 HTTP CONNECT 出站代理和 HTTPS 站点分别记录关联 request ID，凭据/端口由测试环境注入；目标使用域名，测试客户端显式选择本地 mixed 入口且关闭 direct fallback。站点成功与远端代理收到 CONNECT 同时断言；故障节点不能被 DIRECT 路由掩盖。仅清理测试创建的进程/设置，不操作第三方内核。系统感知客户端另在 P2-14 验证系统代理。

当前没有虚构远端代理或 HTTPS 服务实现，P2-13/14 保持 Todo。
