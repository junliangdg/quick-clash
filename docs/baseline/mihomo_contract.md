# Mihomo v1.19.33 Windows 契约记录

2026-10-24后续接入：P2-21—23已用相同官方字节验证主流基础协议的隔离-t、Ready、选择/重启和坏候选保留；GET /proxies适配Selector/URLTest/Fallback，手选入口与恢复只允许Selector。受控HTTP健康检查验证自动组跳过REJECT并选中DIRECT，客户端按操作/显式刷新读取，不另加常驻测速轮询。见[协议交付](../progress/reports/windows_validation.md#订阅与代理功能)及其受控夹具证据；未测新增协议公网握手，Geo/fake-ip仍未接入。下文保留较早探针的实际范围。

2026-10-23使用 [固定资产](release_matrix.json)，程序SHA-256已核验。P0原始输出见 summary.json（原输出已移出版本管理）、api.json（原输出已移出版本管理）。以下保留P0契约记录；2026-10-24生产EngineSession/OwnedProcess/Windows租约与DesktopRuntime已经接入，运行中变更采用受控重启，实际域名CONNECT/TLS与系统代理恢复见 [P2报告](../progress/reports/windows_validation.md#订阅与代理功能)。后文“仍须P2”表达当时探针的边界，不是当前任务状态。

2026-10-24 完整验证见 [当前报告](../progress/reports/windows_validation.md#工程与内核)与[原始汇总](../progress/reports/evidence/core_contract.json)。官方字节、API/名称编码/模式/重载/重启、owned PID 回环监听、Job 子进程回收、零网络能力 AppContainer 正反配置校验全部通过；早期 API-only 和隔离超时失败记录保留。

| 路径 | 实际结果 | 适配决定 |
|---|---|---|
| -v / -h | v1.19.33、go1.26.10，参数已读取 | 官方预编译，参数数组启动 |
| -t -d 目录 -f 文件 | 正例 0，坏 YAML 1；正例未新增文件 | 隔离校验，有界退出 |
| GET /version | 200，meta=true，version=v1.19.33 | 带认证核对版本 |
| 错误 Bearer secret | 401，message=Unauthorized | 不无限重试，不把 secret 放 URL |
| GET /configs | 200，mixed-port、allow-lan、mode 局部字段 | 不反推完整配置 hash |
| GET /proxies | 200，Selector 有 all/now/type | 读真实成员和当前值 |
| PUT /proxies/编码组名 | 中文、空格、/、%、?、# 单一路径段编码成功；204 | 响应后 GET 读回 |
| 选择不存在成员 | 400，错误体存 api.json | 保留旧选择 |
| PATCH /configs mode | global/direct/rule 均 204，GET 一致 | 不提前成功 |
| PUT /configs payload | 204，配置摘要一致 | 使用 payload；P2 另测失败补偿 |
| store-selected=false 重启 | fixture 重置为首个 DIRECT | 应用负责可靠身份恢复 |
| owned 进程退出 | Job KILL_ON_JOB_CLOSE 宿主退出后子进程结束，finally 清理通过 | 产品正常停止/崩溃协调仍在 P2 |

样本无 Geo/provider/外部文件。隔离探针先验证父进程可连接自有回环，而子进程在 1 秒有界非阻塞 connect 中超时 10060，再以相同官方字节在零网络能力容器执行正反 -t、核对无新增文件；不把“无新增文件”单独当作网络隔离。节点为 example.invalid，仅测试控制面，无真实转发/网站访问；域名 CONNECT/受控远端在 P2-13。

Ready 探针核对认证 version/configs 和 OS TCP 表中的当前 owned PID/回环监听。Job 使用 CREATE_SUSPENDED → AssignProcessToJobObject(KILL_ON_JOB_CLOSE) → ResumeThread，core_job_probe 实际编译/回收通过。早期 Get-NetTCPConnection 拒绝访问结果只保留历史；生产适配器仍须在 P2 实现相同所有权约束。

每次随机生成 32 字节 secret，报告脱敏、临时文件 finally 清理；临时 AppContainer profile/ACL 仅作用于自有新建会话。这不是生产 ACL/事务/凭据实现。生产启动超时、端口冲突、响应丢失和故障补偿仍须 P2 验证；服务管道/ACL、TUN、WS/provider 未进入支持声明。
