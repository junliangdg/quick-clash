# 依赖获取与 Geo 固定来源修复

## 范围与状态

- 日期：2026-10-26；关联任务：P0-09（依赖获取/离线包工具）、P0-02（冻结资产来源）；执行者：Zoo。
- 当前状态：Done（依赖获取工具修复范围），11/11定向测试及真实在线/离线获取通过；既有进度产物链接检查Fail单列。不构建产品，不运行CMake配置、CTest、GUI或内核转发。
- 开始时`git status --short`为空；保留工作区，未提交、未创建远端事项。
- 未改变设计范围；本次是获取工具修复，不实现Geo自动更新。

## 故障与官方来源核对

原清单冻结2026-10-24摘要，却使用官方latest下载地址及master许可证地址。官方发布API显示2026-10-26的GeoSite为4,307,247字节、SHA-256 `acbf79349cd32b9b95dba8a3918ea778eb07cb9bb662c258b8d353514953b1dd`，不同于冻结快照。校验拒绝是正确行为，不能靠关闭校验或自动改摘要处理。

官方仓库只有latest发布，数据release分支使用无父提交并每日强制推送；常规历史查询未返回旧快照。通过官方[分支活动记录](https://api.github.com/repos/MetaCubeX/meta-rules-dat/activity?ref=refs/heads/release&per_page=12)找到2026-10-24原提交`bfa68b978ec9c0494203d7c0d64e186640f7257b`；许可证最后变更提交为`964be7423e659538e4aa4b77ff908188b7102890`。从这两个固定提交下载实际字节后，三项均匹配原清单：

| 资源 | 实测字节 | 实测 SHA-256 |
|---|---:|---|
| GeoIP | 16570425 | a6271d3c96361d96156e6d3f7cf1550ef21b00d2f951deb772f464db91e89ac5 |
| GeoSite | 4311865 | 610e2fa2b57b1232ba2220eeea45eb6e0bbf3675e19ac6ee2d29e8eb3e294b10 |
| LICENSE | 35149 | 3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986 |

首次来源核对通过；查询命令曾有一次PowerShell管道语法错误，修正后成功，不是下载脚本失败。补丁上下文曾匹配失败，未写入文件，修正后应用。未将失败历史记为Pass。

## 实现

- [清单](../../baseline/release_matrix.json)记录完整数据/许可证提交，使用固定提交raw地址；不改原摘要、大小、快照日期、Mihomo版本或字节。
- [获取脚本](../../../scripts/fetch_dependencies.ps1)明确PowerShell 7.2最低版本，共用大小/摘要校验；错误包含来源、路径、预期/实际摘要或大小。
- 下载先校验再发布缓存，失败时清理临时文件；已有缓存不自动覆盖，离线缺失报告恢复路径。
- [定向测试](../../../tests/scripts/fetch_dependencies_test.ps1)在隔离且含空格的仓库夹具中调用真实脚本，模拟网络成功/错误；不访问外网，不构建产品。
- [构建说明](../../baseline/build_and_package.md)区分yaml-cpp配置依赖、完整运行资源与显式Geo基线更新。

## 验证记录

| 检查 | 状态 | 说明 |
|---|---|---|
| 官方固定提交三项实际下载/原摘要核对 | Pass | [来源审计](dependency_fetch_evidence/source_audit.json)，见上表；仅证明资产来源与字节一致 |
| 下载脚本11项定向回归 | Pass | [最终日志](dependency_fetch_evidence/script_tests.log)，退出0；首轮夹具网络模拟函数误用脚本作用域，写入空字节数组失败，改共享状态后11/11通过 |
| 当前工作区完整依赖获取 | Pass | [在线日志](dependency_fetch_evidence/fetch_online.log)，退出0；真实下载缺失GeoSite/许可证并核对冻结摘要，原失败临时文件已清理 |
| 完整依赖离线复核及仅yaml-cpp离线复核 | Pass | [完整离线](dependency_fetch_evidence/fetch_offline.log)、[仅YAML离线](dependency_fetch_evidence/yaml_offline.log)，两条入口均退出0 |
| 进度检查 | Fail | [日志](dependency_fetch_evidence/progress.log)，退出1；既有报告链接到当前不存在的dist/proxy_memory_fixes/bin/quickclash.exe，[HEAD核对](dependency_fetch_evidence/progress_baseline_issue.log)确认非本次引入；不伪造产物、不为此构建 |
| PowerShell语法、命名、冻结基线、差异检查 | Pass | [静态日志](dependency_fetch_evidence/static_checks.log)、[命名日志](dependency_fetch_evidence/source_layout.log)，退出0；35目录/134文件，PowerShell 7.6.6，diff仅LF/CRLF提示 |
| 产品构建/运行/GUI/转发 | NotRun | 按用户约定不构建；本次无产品源码/数据字节变更 |

复验入口（仓库根目录，PowerShell 7.2及以上）：

```powershell
pwsh -NoProfile -File tests/scripts/fetch_dependencies_test.ps1
pwsh -NoProfile -File scripts/fetch_dependencies.ps1
pwsh -NoProfile -File scripts/fetch_dependencies.ps1 -Offline
pwsh -NoProfile -File scripts/fetch_dependencies.ps1 -YamlOnly -Offline
pwsh -NoProfile -File scripts/check_progress.ps1
pwsh -NoProfile -File scripts/check_source_layout.ps1
git diff --check
```

进度检查失败后，原组合命令未继续语法/命名/差异步骤；随后独立运行并通过，未把未执行的步骤记为Pass。完整获取只复用已通过校验的yaml-cpp/Mihomo/GeoIP缓存，不冒称所有资产均重新下载；三项Geo/许可证另有独立固定提交实际下载审计。

## 证据有效性与下一步

P0-02/P0-09既有Done仅保留历史交付含义，本次工具修复验证单列，不以文档或资产校验冒充产品验收。原Geo数据摘要/字节未变，不使历史产品资源结果失效；G0/G1保留历史基线，G2/G3及MVP状态不变。

本次修复范围已完成，当前依赖缓存齐备，可在IDE重新执行CMake Configure；未实际运行配置，不预先宣称产品构建通过。历史进度链接缺失留待包/证据维护时处理，不阻塞本次依赖获取修复；P3-01节点选择/测速的原下一任务不变。

上游可能回收被强推替换的旧提交，应保留已验证的离线缓存；固定提交URL不承诺永久可下载。需要升级Geo必须显式更新清单并验证，不在初始化过程中自动接受新字节。
