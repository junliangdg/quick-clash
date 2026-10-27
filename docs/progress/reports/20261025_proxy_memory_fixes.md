# 代理出口展示与订阅更新内存修复

日期：2026-10-25；执行者：Codex；范围：用户反馈的三项回归，关联P3-01/02/06。工作区增量交付，未提交；不代表整个P3或G2/G3通过。

## 1. 交付行为

- 代理组按订阅proxy-groups原顺序显示，实时now/all由Mihomo读回覆盖；修复遍历JSON对象导致的字典序排列。代理页按订阅记忆“正在查看分组”，默认定位当前模式的默认出口组；查看分组不改变路由。
- 首页显示精简的默认组→最终节点，代理页显示嵌套出口链。规则模式依据MATCH，GLOBAL用于全局，DIRECT用于直连；无MATCH不猜首组，负载均衡不虚构单个节点。其他匹配规则及已有连接可使用其他出口。仅Ready且选择恢复完成才显示已确认；相关内容可见时每5秒合并读取成员，隐藏即释放，不进行后台测速。
- 首页内存分为QuickClash客户端与Mihomo内核，窄屏上下排列；专用工作集为主指标，工作集/私有提交独立标注，十进制MB，未知显示破折号。客户端包含运行中的必要辅助服务，内核单列。英文源文案与中文词表同步。

实现位置：src/infra/config/config_preflight.cpp、src/application/desktop_runtime.cpp、desktop_features.cpp、qml/components/outbound_status.qml、qml/features/proxies/及home/home_page.qml。

## 2. 内存原因与处理

已确认的主要原因是QIODevice::read(maxSize)可能在短读后保留maxSize容量。JsonStore读取约500KB配置时传入16MiB策略上限，多个跨回调保存的QByteArray由此保留过大缓冲；订阅/provider下载也按上限申请临时读取量。改为实际文件长度+1或当前可读字节，仍保留原文件/下载大小限制与错误检查，没有调用工作集裁剪API。

同时减少更新过程的重叠分配：

- Geo核验和复制改用64KiB流式处理，大小/摘要均通过后原子提交。
- 有界事件读取器提取元数据、逐条验证规则，避免格式识别/provider发现/节点身份/组摘要/Geo需求反复建立完整规则Node图；跳过字段仍检查重复键、别名、多文档、深度和事件数。完整schema仍校验其他字段，官方内核-t仍保留。
- 新内容通过预检和官方-t后，立即应用复用该次语义产物；规范块根配置仅追加安全转义的控制secret，不再完整Load/serialize。旧flow根产物保留兼容路径。
- 无provider且下载摘要及保存源字节均相同，复用此前验证的源，更新来源凭据/时间/用量；待应用与自动应用策略保留。provider和新内容仍完整获取/验证。
- QML订阅卡片使用独立profiles属性，仅列表内容变化时通知，避免流量/状态通知反复重绑卡片。

实现位置：src/infra/storage/json_store.cpp、src/infra/config/config_metadata.*、config_preflight.cpp、config_schema.*、geo_resources.cpp、src/application/desktop_runtime.cpp、desktop_providers.cpp、src/presentation/ui_state.*。公开定向探针位于tests/integration/subscription_memory_probe.*，不包含用户订阅。

## 3. 同场景GUI测量

Windows/Qt6.8.0/MSVC Release，真实Qt Quick订阅页，生成的本机HTTPS公开夹具：56节点、12,001条规则、508,300B、内部DNS及固定Geo；官方Mihomo v1.19.33。独立线程每10ms采集客户端进程Windows内存计数，含未变更新、有变更新及完成后5秒，共约13秒。没有运行辅助服务、没有写系统代理或开启TUN，内核不计入下表。各列为该阶段独立峰值。

| 阶段 | 专用工作集MB | 工作集MB | 私有提交MB |
|---|---:|---:|---:|
| 修复前有变更新 | 105.81 | 209.71 | 306.22 |
| 最终包更新前 | 72.01 | 175.64 | 226.76 |
| 最终包内容未变更新 | 72.87 | 176.50 | 222.79 |
| 最终包内容有变更新 | **76.45** | **180.10** | **228.35** |
| 最终包完成5秒 | 76.34 | 180.15 | 224.40 |

最终包有变更新峰值76,451,840B低于100,000,000B目标，工作集180,097,024B、私有提交228,347,904B；完成后专用工作集76,341,248B。未变保持原内核PID，有变确认新selectionGeneration及Ready，error为空，进程退出0。最终包与构建客户端SHA-256相同：b75969507325080b83439a5596d1fb1478840076d4860402a5e8b828cbdff85f。[最终包采样](proxy_memory_evidence/update_memory_gui_package.log)及对应.log.json。此前01377351…采样值75.26MB为缓冲修正后的构建，最后显式标准库include调整改变二进制，已对最终包补做同一场景核对，没有挪用旧SHA资源值。

只做前几项去重时峰值仍在106—114MB；紧邻缓冲修正之前为106.07MB专用工作集/211.99MB工作集/320.23MB私有提交，缓冲修正后降至75.26/181.30/224.29MB。不能把早期优化误称为已达到目标。原始记录见[初始GUI](proxy_memory_evidence/update_memory_gui_before.log)、[缓冲修正前](proxy_memory_evidence/update_memory_gui_rules.log)、[最终GUI](proxy_memory_evidence/update_memory_gui_buffers.log)及对应.log.json。

未复现用户反馈的700MB工作集或持续200MB专用工作集；当前夹具无持续增长证据，不将所有内存问题定性为泄漏，也不推断私有订阅/任意大配置/长期运行全部满足指标。真实私有订阅及节点凭据未进入仓库或日志。

## 4. 构建与回归

- Windows Release /WX增量构建通过（quickclash、service及受影响测试）。[构建日志](proxy_memory_evidence/buffer_build.log)。
- storage/contracts/runtime/localization/ui_state/qml_smoke/ui_responsive共7/7通过，75.09秒。[测试日志](proxy_memory_evidence/buffer_tests.log)。runtime覆盖官方核验/更新后受控转发及失败保留；本轮使用注入系统代理，未重新操作真实WinINet。
- QML lint退出0；既有runtime_list可调用var、Qt6.8 Dialogs重复类型及订阅页hovered静态警告保留，未将退出0写作零警告。[lint日志](proxy_memory_evidence/fixes_qmllint.log)。
- 契约覆盖非字典序分组、嵌套出口、三模式、无默认规则、负载均衡；事件读取的跳过字段重复键/别名/多文档反例及flow旧产物/secret转义。
- 定位中的首次探针构建因Windows头顺序失败，flow夹具遗漏必需proxy-groups导致契约失败，深层非map根暴露错误优先级变化；分别修正include顺序、夹具和前置预算扫描后通过。旧失败日志留在本地build目录，不覆盖为Pass。

## 5. 交付与后续

该次验证包路径为dist/proxy_memory_fixes/bin/quickclash.exe；本地包现已缺失，包可用性为Stale，不再保留失效的可下载链接。下面的原始运行日志仅证明当时构建，重新使用须重建完整目录并核对摘要。186个文件大小/摘要全部复核，官方内核SHA-256为1cde41cfffd3de0aedad63c0a8515e1ce5eea507e8133aed0f637d316c93800a（v1.19.33官方字节不变），[摘要结果](proxy_memory_evidence/package_verification.json)。清Qt开发PATH后的随包smoke通过，安装部署仅保留缺dxcompiler/dxil及VCINSTALLDIR提示，离线包补齐CRT。显式include调整后又通过Windows增量及contracts，见[最终构建](proxy_memory_evidence/final_headers_build.log)与[契约](proxy_memory_evidence/final_headers_contracts.log)。中文真实配置[代理页](proxy_memory_evidence/proxies_zh.png)和[首页](proxy_memory_evidence/home_zh.png)已目视核对，默认z_main→a_auto→node_alpha、首页精简z_main→node_alpha，客户端/内核标签和缺失值布局清晰，两次qml_errors均0；截图在首个Ready取值，未知内核采样显示破折号，不能把图中瞬时值当内存验收。探针退出0，未改系统代理/TUN，未停止用户原实例。完整P3-01/02/06仍有原任务范围，保留原状态；本修复切片完成不等于整个P3/G2/G3通过。旧包的出口UI及资源证据对本次变更Stale；MVP-05/06历史未受影响范围保留，本轮runtime不扩充真实系统代理验收。

设计分册与合并生成/-Check、check_progress、check_source_layout（35目录/133文件）、全部受影响C++ clang-format --dry-run --Werror和git diff --check均通过；差异检查只有LF→CRLF提示。新证据只含公开本机夹具，私有订阅URL/查询凭据未保存。工程脚本通过不等于新增产品阶段Pass。

下一功能仍为P3-01完整选择/测速页面核对与交付，再按P3-02/06剩余范围推进；不增加多平台、长时或重复资源矩阵。

