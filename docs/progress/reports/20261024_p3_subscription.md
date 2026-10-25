# P3 私有订阅最小可用链路

2026-10-24开发、2026-10-25交付 / Codex，P3-10 Done。用户指定代表订阅S3已走生产导入/选择/启动路径，真实VLESS节点在全局模式完成HTTPS200、完整1MiB、默认TLS验证及连接链核对。URL/原始配置/节点凭据只在仓库外临时目录，仓库仅保存匿名结构与状态。保留全部共享工作区成果，不创建提交或推送，不重置期间出现的已有提交。

初始观察：HTTP200，正文48,743字节、MIME误标text/html；结构识别成功，包含56节点（Hysteria2 20/VLESS 36）、3组（select/url-test/fallback各1）。规则为DOMAIN16、DOMAIN-KEYWORD40、DOMAIN-SUFFIX422、GEOIP1、IP-CIDR32、IP-CIDR6 4、MATCH1，有fake-ip DNS。原生产预检返回UnsupportedConfigFeature，不能导入；本次保留全部原规则和已支持DNS语义，不删规则绕过。

P3-02/08保留完整后续范围，本任务依赖已完成P2-07/21/22/23，不以P2-17阶段汇总缺口阻塞独立功能开发。P3现1Done/9Todo，G2仍Blocked，G3仍NotRun。

## 实现及边界

- src/infra/config/config_schema.cpp、config_preflight.cpp：补DNS字段/列表/解析器协议/引导校验、GEOIP/GEOSITE与TLS指纹，Hysteria2端口跳跃ports/hop-interval；mport规范化为ports、冲突拒绝。VLESS空flow可用于WS等传输，非空Vision仍约束TCP+TLS。原始订阅单独存储，语义模板关闭TUN/LAN，DNS listen置空，geo-auto-update=false；不改变系统DNS。有效配置预览遮蔽DNS及节点凭据。
- src/infra/config/geo_resources.*：从release_matrix.json编译固定SHA，离线校验包内字节后原子复制GeoIP.dat/GeoSite.dat到AppContainer staging或owned核心data/core_state。校验、启动采用同快照；resourceManifestRef记录文件名/摘要，重开和启动核对清单。缺失/损坏明确拒绝，不隐式下载。既有无Geo配置的空资源清单兼容。
- Geo来自MetaCubeX/meta-rules-dat的2026-10-24快照：GeoIP 16,570,425B/SHA a6271d3c96361d96156e6d3f7cf1550ef21b00d2f951deb772f464db91e89ac5，GeoSite 4,311,865B/SHA 610e2fa2b57b1232ba2220eeea45eb6e0bbf3675e19ac6ee2d29e8eb3e294b10；匹配上游校验和，GPL-3.0原文/摘要随包。下载URL中的latest可变，冻结摘要才是接纳依据；新字节需显式更新基线。当前固定快照不承诺跨应用版本动态资源回退，P3-02另行实现。
- tests/integration/subscription_probe.*：显式--subscription-e2e从stdin读取URL，临时数据目录与随机端口，先走生产导入/选择/启动/节点选择，再经显式HTTP代理访问公共HTTPS目标；默认TLS校验，核对响应大小与认证API连接链。最多六个按协议交错候选，只输出匿名统计，非默认联网CTest，不调用会输出完整私有snapshot的通用失败帮助函数。

## 实际检查

| 项目 | 结果 | 证据/限制 |
|---|---|---|
| Windows Release/WX增量构建 | Pass，退出0 | [build_final.log](p3_evidence/build_final.log)、[元数据](p3_evidence/build_final.log.json)；Qt6.8.0、MSVC19.50、CMake4.3，build/p2_responsive |
| contracts/runtime/localization/ui_state | Pass，4/4，23.03秒 | [输出](p3_evidence/tests_final.log)；含公开DNS/Geo官方隔离-t与Ready，基础受控CONNECT/自动组仍通过 |
| 代表S3生产链路 | Pass | [匿名结果](p3_evidence/subscription_e2e.json)；56节点/3组，第二候选VLESS，全局模式，HTTP200、1,048,576B、TLS正常，链包含所选节点且无DIRECT |
| Geo有效/缺失/损坏 | Pass，预期退出0/1/1 | [结果](p3_evidence/geo_integrity.json)；独立包副本，原包与开发资源未改 |
| 清Qt开发PATH随包启动 | Pass，退出0 | [日志](p3_evidence/package_smoke.log)、[元数据](p3_evidence/package_smoke.log.json)，隔离实例，无系统代理变更 |
| 单轮10秒资源短测 | Pass，92样本 | [summary](p3_evidence/runtime_memory/summary.json)、[原始CSV](p3_evidence/runtime_memory/memory.csv)、[清理](p3_evidence/runtime_memory/cleanup.json)；真实Qt Quick首页+公开DNS/Geo配置Ready |
| 最终包文件清单 | Pass，186个文件大小/摘要相符 | [核对结果](p3_evidence/package_check.json)、[清单](p3_evidence/package_manifest.json)；收尾仅刷新用户说明/策略/版本元数据，没有重建客户端 |

公网目标为https://speed.cloudflare.com/__down?bytes=1048576。首个Hysteria2候选未得到成功响应，第二个VLESS成功；不判定具体失败原因，不宣称Hysteria2握手或全部56节点可用。原规则保留，但转发证明使用全局模式，因此GEOIP/GEOSITE具体命中、规则模式公网转发、DNS缓存/回退/循环完整矩阵尚未验收。首次真实成功后产品仅补失败错误保留，最终受影响回归及公开DNS/Geo Ready已重跑；不因文档收尾重复公网或性能矩阵。

| 进程范围 | 专用工作集峰值B | 总工作集峰值B | 私有提交峰值B |
|---|---:|---:|---:|
| 客户端进程组（无额外辅助进程） | 61,796,352 | 157,601,792 | 204,316,672 |
| 独立Mihomo | 49,164,288 | 70,381,568 | 107,769,856 |

客户端专用工作集约61.80MB，小于100,000,000B；其他两项诊断留档，内核单列。退出0且owned核心已回收。仅公开DNS/Geo Ready首页10秒，不覆盖私有56节点导入峰值、持续转发、隐藏重建、CPU/帧/GPU或长稳。旧包资源结论对新二进制Stale，历史UI/系统代理证据按未改范围保留，不扩大原MVP-05/06 Pass。

## Windows产物与未完成事项

入口D:/repos/quick-clash/dist/p3_subscription/bin/quickclash.exe，需保留整个dist/p3_subscription目录；客户端SHA-256为14c601fead90519d852212dc342c00d974163bdfc1111b6259e902c0ee209deb。官方Mihomo仍v1.19.33，SHA-256 1cde41cfffd3de0aedad63c0a8515e1ce5eea507e8133aed0f637d316c93800a，未改内核。包含Qt/CRT、Geo数据/许可及metadata清单，本地开发验证、未签名；普通用户干净环境/公开发行源码义务未验收。旧QuickClash需正常退出后再启动新包，否则单实例只会激活旧版本。

开发中发现并修复两类实际问题：首次资源清单未包含JsonStore自动写入的schemaVersion，导致runtime回归失败；VLESS空flow被过度约束，导致S3预检拒绝。修复后最终相关测试通过，旧失败不伪称成功。所有私有URL/正文/节点信息仍仓库外；最小证据只记录计数/协议/状态。未操作用户现有实例、系统代理或系统DNS。

下一功能任务P3-01先实现筛选及按需节点延迟；P3-02继续计划/用量、provider/RULE-SET与动态资源更新/回退，P3-08继续高级DNS策略和设置。连接/日志/规则完整页、TUN服务、资源信息等仍待开发。P2-11/15/16阶段缺口另记，不增长时/多轮验证矩阵。

设计合并生成及-Check、check_progress、check_source_layout、PowerShell/JSON/XML解析及git diff --check均退出0，见[交付检查](p3_evidence/delivery_checks.log)。10个涉及C++文件clang-format只读核对退出0，见[格式元数据](p3_evidence/cpp_format.log.json)。仓库文本隐私启发式扫描829个文件，私有匹配0，排除2个精确公开测试哑值；首次宽泛检查将这两个哑值命中，复核后使用精确例外，见[隐私结果](p3_evidence/privacy_check.json)。初次刷新包清单时路径分隔符替换转义失败，已改用字符编码并重新核对186文件；没有把空清单当作通过。上述工程脚本不替代产品验收。
