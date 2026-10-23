# Mihomo v1.19.33 Windows 契约记录

2026-10-23 使用 [固定资产](release_matrix.json)，程序 SHA-256 已核验。输出见 [summary.json](../progress/reports/p0_core/summary.json)、[api.json](../progress/reports/p0_core/api.json)。执行 scripts/probe_mihomo.ps1 -ApiOnly，退出码 0；生产适配器属于 P2。

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
| owned 进程退出 | 强制退出 5 秒内结束，finally 清理 | 正常停止/Job 仍需测试 |

样本无 Geo/provider/外部文件；未抓包或强制断网，不能把“无新增文件”解释为全部配置无网络副作用。节点为 example.invalid，仅测试控制面，无真实转发/网站访问；域名 CONNECT/受控远端证据在 P2-13。

Ready 脚本检查认证 version、configs；产品仍须核对当前 owned PID 的监听归属。Get-NetTCPConnection 本次拒绝访问，OS 归属 NotRun。Job 源码实施 CREATE_SUSPENDED → AssignProcessToJobObject(KILL_ON_JOB_CLOSE) → ResumeThread，须构建 core_job_probe 后杀宿主核对子进程结束；本次未编译/运行，P0-04 未完成。

每次随机生成 32 字节 secret，报告脱敏、临时文件 finally 清理；这不是生产 ACL/事务/凭据实现。启动超时、端口冲突、响应丢失、离线隔离和故障补偿仍待验证；服务管道/ACL、TUN、WS/provider 未进入支持声明。
