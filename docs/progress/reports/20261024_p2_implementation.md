# Windows P2 订阅与代理功能交付

后续UI/语言/订阅协商及新包见[本轮追加交付](20261024_p2_responsive_localization.md)。本报告是1.6的历史运行记录；旧包UI/下载/资源结论对当前改动为Stale，未改核心/WinINet证据按范围保留。新增协议规划见[分期任务](20261024_mihomo_feature_plan.md)，不得把旧子集通过扩展到新协议。

日期/执行者：2026-10-24 / Codex。源码基于 HEAD 3b12f63510afdb1a95e863e7852fb08e06aafef4 加本次未提交工作区修改，未自动提交或推送。设计基线1.6；本报告记录实际实现与必要短测，保留历史失败。

结论：Windows 下已形成“本地/HTTPS导入 → 校验 → 选择 → 官方内核 → 节点/模式 → 系统代理 → 受控HTTPS转发 → 停止恢复”的可运行链路。P2共17项中13项Done、2项Review、2项Blocked；G2仍Blocked。真实OS事件与客户端强杀恢复、干净普通用户环境及部分失败组合仍待核对。不能把开发机通过称为全部16项MVP通过。

## 产物与实现

本地验证包：D:/repos/quick-clash/dist/p2_final/bin/quickclash.exe。完整目录使用，不单独移动exe。客户端SHA-256：075d3f620f1c67e0df47551cec35597481a2cfb7ae052526fb6cf39bb4d21767。该包开启BUILD_TESTING，包含有界隔离探针；正式发行须另做关闭测试探针的构建。未签名，非公网发行包，也未在干净普通用户系统验证。

参考环境为Windows NT 10.0.26300 x64、Qt6.8.0 msvc2022_64、MSVC19.50、CMake4.3、yaml-cpp0.9.0。Mihomo固定v1.19.33官方amd64基线资产，程序SHA-256为1cde41cfffd3de0aedad63c0a8515e1ce5eea507e8133aed0f637d316c93800a；未修改或编译Go内核。

| 实现位置 | 本次行为 |
|---|---|
| src/infra/storage/ | 单写者锁、schema1、有界原子JSON/文件、当前用户/SYSTEM ACL、manifest权威、提交前后日志恢复、偏好异步保存 |
| src/platform/windows/credential_store.* | 订阅URL/受限请求头/测试目标与身份密钥使用当前用户DPAPI，失败无明文降级 |
| src/platform/windows/owned_process.*、src/infra/core/engine_session.* | 官方摘要与版本、挂起创建后归入kill-on-close Job、回环端口PID归属、认证Ready、请求有界/过期抑制、隔离AppContainer校验 |
| src/application/desktop_runtime.* | 长驻worker协调导入、不可变原文/有效配置、事务应用及补偿、节点HMAC身份保存与恢复、有限崩溃重启、垃圾回收 |
| src/platform/windows/system_proxy.* | 当前用户WinINet LAN快照/写入/读回、持久租约、幂等、端口迁移、PAC/绕过/自动发现恢复、外部冲突及显式重新开启 |
| app/system_events.* | QNetworkInformation与Windows唤醒事件接入，1.5秒合并核对；不自动更新订阅或测试网站 |
| qml/features/、src/presentation/、app/application_host.* | 首页启停/模式/系统代理、真实组选择、订阅导入/使用/更新/名称/来源头/删除、端口/错误/脱敏预览/修复入口、托盘真实命令 |
| scripts/package_windows.ps1、bench/measure_runtime_windows.ps1 | Qt/QML/TLS、显式VC CRT、官方内核/许可证/全文件摘要打包；真实GUI与Ready内核的单轮三指标采样 |

下载使用独立NoProxy上下文，最大16MiB、15秒传输/20秒整体截止，HTTPS默认校验证书且拒绝重定向；最多100个配置。更换来源先下载并通过预检/隔离-t，成功再替换来源记录，运行版本须明确“使用”才更新。头仅接受Authorization/User-Agent/Accept。停止使旧下载generation失效；活动配置删除先停止并核对代理恢复，最后一个配置删除后回未配置。

运行中配置/模式/端口变更统一采用受控重启和旧确认版本补偿，尚未在产品路径启用热重载。Ready后先恢复可靠身份组选择并读回，再写系统代理，最后提交manifest。源节点秘密仍存在受限ACL的原文/运行配置中，不能宣称整个数据目录均为密文；预览遮蔽节点敏感字段，日志只存100条时间/稳定错误码。

网站测试是用户自设HTTPS目标、显式当前mixed入口、HEAD、整体12秒/并发1、无直连回退。组/模式/网络变更取消旧结果；普通网站成功仅说明当前入口可达，受控端到端夹具另外证明代理出站。实时流量、完整连接/规则页、批量测速、TUN、provider/Geo/IP与资源卡仍属后续开发，界面不伪造业务数据。

## 已运行的必要检查

以下为真实命令结果，文档检查不计为产品验收。加载VS开发环境后构建，Qt工具不修改安装文件，也不全局降低自有源码警告。

| 检查 | 结果与证据 |
|---|---|
| cmake --build build/p1_windows | 最终Release增量构建退出0，自有代码/WX；[构建日志](p2_evidence/20261024/p2_final_build.log) |
| cmake --build build/p1_windows --target quickclash_qmllint | 退出0；Qt6.8 Dialogs导入仍有重复类型警告，不能称无警告；[日志](p2_evidence/20261024/p2_delivery_qmllint.log) |
| ctest --test-dir build/p1_windows --output-on-failure --output-junit p2_final_delivery.xml | 最终代码10/10 Pass，24.20秒；[JUnit](p2_evidence/20261024/ctest_final.xml)、[日志](p2_evidence/20261024/p2_final_ctest.log) |
| headless增量构建与CTest | 2/2 Pass；只有Qt Core和领域/配置/仓库，无Quick/Widgets/Network；[JUnit](p2_evidence/20261024/headless.xml)。最终两处runtime修正不影响此目标 |
| runtime_test.exe --native-system-proxy | 最终代码退出0；真实WinINet PRECONFIG HTTPS经过受控CONNECT代理，重复开启及迁移端口后退出，系统原快照exact相等；退出意图保留、重开不启动、UserStop清除通过；[结果](p2_evidence/20261024/runtime_native_final.json)、[日志](p2_evidence/20261024/p2_final_native.log) |
| package_windows.ps1 -Destination dist/p2_final | 退出0；清开发Qt PATH后使用随包插件smoke；[日志](p2_evidence/20261024/p2_package_final.log)、[smoke](p2_evidence/20261024/package_smoke_final.json)、[包清单](p2_evidence/20261024/package_manifest_final.json) |
| measure_runtime_windows.ps1 -Executable dist/p2_final/bin/quickclash.exe -OutputDirectory docs/progress/reports/p2_evidence/20261024/runtime_memory_final -Seconds 10 | 最终包单轮10秒91样本，专用工作集短测Pass；[汇总](p2_evidence/20261024/runtime_memory_final/summary.json)、[CSV](p2_evidence/20261024/runtime_memory_final/memory.csv)、[清理](p2_evidence/20261024/runtime_memory_final/cleanup.json) |

storage检查包含单写者、非法路径、不可变文件、首次空历史、manifest前中断/后日志未终结、偏好失败保留、过新/损坏schema只读、DPAPI损坏拒绝。proxy_lease在注入适配器验证幂等/迁移、PAC/绕过恢复、外部冲突/显式重开、已有租约中断恢复、OS写失败与磁盘只读时仍恢复匹配值；不修改真实OS。

runtime默认测试实际执行本地及HTTPS导入、零网络能力-t、停机选择、owned Ready、编码名称选择读回、模式重启、重开恢复确认节点、脱敏预览、独立TLS GET与CONNECT、当前入口HEAD、坏更新保留旧会话、来源更换/重命名/删除、占用端口补偿、杀owned内核后恢复、20次模拟reconcile、最后条目删除和停止Operation终态。[默认详细运行](p2_evidence/20261024/runtime_delivery.json)记录最后意图补充前同一组断言，最终代码已在[最终JUnit](p2_evidence/20261024/ctest_final.xml)复跑，原生专项也复用整条链路。单次崩溃恢复不等于连续三次耗尽/停止打断全部实测；20次调用后稳定不等于已精确统计请求数。

最终新增代理意图独立原子文件config/proxy_intent.json，只有成功持久化才确认开关；写入失败撤回本次仍拥有的租约。组合首次启动提交失败尝试恢复之前意图；退出/AppExit保留意图、UserStop清除，界面与托盘通过同一应用门面调用。最终原生专项复跑并实际断言意图重开/停止语义；旧原生与默认证据留存，不覆盖历史。测试完成核对恢复原值；不保存真实用户代理快照。

TLS/CONNECT夹具均为本机独立逻辑服务，保留域名fixture.quickclash.invalid由上游fixture映射到回环TLS站点。因此证明Mihomo走了代理协议出站，不能据此宣称公网跨机网络已验收。Qt仅测试进程信任公开测试证书，WinINet仅该请求允许测试CA；产品没有TLS绕过、OS信任库未修改。公开测试密钥见tests/fixtures/network，不是真实节点/用户秘密，禁止复用生产。

## 资源短测

| 进程范围 | 专用工作集峰值 | 总工作集峰值 | 私有提交峰值 |
|---|---:|---:|---:|
| 客户端组，1进程/无辅助客户端 | 55,054,336 B（55.1 MB） | 148,557,824 B | 200,167,424 B |
| owned Mihomo，单列 | 7,647,232 B | 27,959,296 B | 63,508,480 B |

仅专用工作集参与≤100,000,000 B判断，没有使用工作集裁剪。场景为最终包真实Qt Quick首页+持久仓库worker+本地导入/选择后的Ready内核，不是回放；采样未开启系统代理、无持续转发，也未包含导入峰值/隐藏重建/CPU/帧/GPU/长期。client退出0且owned内核退出。G2-E/MVP-08的正常组合操作完整结论仍未取得，不因这个单场景通过自动勾选。旧runtime_memory和runtime_memory_delivery二进制5a28…/0877…的读数保留为历史，对最终包已Stale，不能替代本次记录。

G0/G1的历史工程/原型基线仍Pass；最终10项测试和上述短测复核工程壳、回放/宿主及首页资源。P1旧截图/资源不再证明本次新增订阅/设置业务页的视觉与组合负载，受影响部分Stale；无需重复完整矩阵。

## 逐任务结论

| ID | 状态 | 实现/验收范围与剩余项 |
|---|---|---|
| P2-01 | Done | 原子仓库/schema/manifest与关键中断窗口单元验证；完整外部故障组合仍由P2-15汇总 |
| P2-02 | Done | 偏好异步持久化、P1迁移、DPAPI与默认安全值；代理意图专门契约缺口归P2-09 |
| P2-03 | Done | 官方字节、认证/Ready/所有权/Job、隔离校验与停止；当前停止直接终止owned Job，不宣称正常退出 |
| P2-04 | Done | 严格输入子集、原文/语义/会话区分、停机选择、空历史、隔离校验 |
| P2-05 | Done | 首次/运行事务、节点恢复后提交、端口失败补偿、manifest权威；剩余组合故障在P2-15 |
| P2-06 | Done | 首页真实状态/组选择与可靠身份恢复/模式/遮蔽配置预览；实时流量与完整资源后续 |
| P2-07 | Done | 本地/HTTPS导入、来源/受限头替换、更新/名称/删除；活动更新保留旧运行版本 |
| P2-08 | Done | WinINet LAN租约/快照/冲突/重开/迁移与磁盘失败恢复，范围不含WinHTTP/RAS/其他用户 |
| P2-09 | Done | 组合门面/停止优先/有限崩溃恢复/退出清理，意图独立持久化及失败补偿，AppExit保留/UserStop清除实测；强杀组合由P2-15覆盖 |
| P2-10 | Done | 无内核错误/改端口/导入/选择/停止恢复入口、数据目录、手工恢复说明；损坏存储保留不覆盖 |
| P2-11 | Review | OS事件接入和合并逻辑已实现/模拟调用已跑；未实际触发换网/休眠，解除需一次代表OS事件与请求计数核对 |
| P2-12 | Done | 用户目标DPAPI保存、显式当前入口HEAD、超时并发限制/无直连回退；结构化错误/GET/批量在P3 |
| P2-13 | Done | 受控HTTPS与独立CONNECT出站fixture实际GET、域名目标和远端接收记录通过，限制为回环夹具 |
| P2-14 | Done | 真实系统感知HTTPS客户端、重复租约、端口迁移及原值恢复；PAC/外部冲突另由注入适配器覆盖 |
| P2-15 | Blocked | 部分manifest故障/占用端口/坏更新/核心崩溃已测；等待P2-11，缺客户端强杀→下次恢复和首次平台/落盘失败组合 |
| P2-16 | Review | 完整本地包/CRT/官方内核/摘要/用户说明和随包smoke齐全；缺无Qt/开发工具的普通用户干净环境 |
| P2-17 | Blocked | 已汇总当前MVP与短测；依赖P2-15/16未关闭，不能宣称G2通过 |

MVP-05/06可按本次范围Pass：Ready后实际系统写入和独立系统客户端、显式本地入口与受控代理HTTPS分别有记录。MVP-04的节点/模式成功路径、MVP-14的租约路径已有较充分证据，但失败组合及真实外部冲突范围仍需核对，整项先NotRun。其他条目在CHECKLISTS明确记录已测子条件与缺口，不把一个子断言通过冒充整条。

## 限制、失败历史与接续

本轮曾修正Windows头文件次序、隔离验证参数/权限、取消Operation终态、键盘测试回放入口，以及部署脚本继承旧LASTEXITCODE误报。一次未加载VS环境的lint触发utility头缺失，重新加载VS后的最终构建/lint已退出0。Qt Dialogs导入警告仍存在；windeployqt找不到dxcompiler/dxil及VCINSTALLDIR提示仍保留，CRT由打包脚本显式复制，默认D3D11，未宣称D3D12或干净机通过。

存储按单项大小/数量限额与GC保留selected/active/lastGood及5个近期版本、32条事务、catalog来源/使用中的DPAPI引用；总磁盘配额及完整备份/回退UI尚未开发。失败下载生成的孤儿凭据可到下一次GC才清理；运行协调器仍集中于DesktopRuntime，IEngineSession抽象尚未由具体适配器完整实现。完整clang-tidy、非Windows原生/CI及长时图形矩阵未运行。

下一任务P2-11：选一次代表OS事件核对，随后P2-15客户端强杀/下次租约及关键首次失败窗口；P2-16用干净普通用户环境核对，更新P2-17/G2。当前Job固定强制停止已在设计/指南明确，正常退出优化后续按需。继续功能开发，不展开重复性能矩阵。

项目许可及Qt/Mihomo完整对应源码提供方案仍待发行工作落实；随包许可证和上游链接不等于已满足公网分发义务。[Windows使用与恢复说明](../../baseline/windows_user_guide.md)可用。交付前设计合并生成及-Check、check_progress、check_source_layout、全部自有C++ clang-format检查、两个新增PowerShell脚本解析、基线JSON、包/采样二进制SHA一致性与git diff --check均退出0，命名覆盖33目录/99文件，diff仅有LF→CRLF提示；这些不替代上述产品结果。
