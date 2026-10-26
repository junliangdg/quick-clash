# 客户端内存优化（持续报告）

日期：2026-10-26；负责人：Zoo；状态：**P0/P1/P2及独立授权优化P3实现/Review，C++/GUI/产品验证NotRun**。此处P0—P3为优化优先级，不是仓库阶段。第1—21节保留P0/P1/P2历史切片；本轮订阅卡片/隐藏与最小化卸载优化P3见第22节以后，不将仓库P3任务标Done。工作区基于45229be，本轮开始47修改/67未跟踪共114路径，未提交；最终路径核对见后续检查记录。

## 1. 边界与身份核对

- 禁止构建：未运行CMake配置/生成/编译、产品打包或旧产品二进制；不控制用户QuickClash/Mihomo，不请求真实订阅，不采集进程命令行。
- 使用只读进程查询（仅按客户端/服务名称，不取命令行）：未发现匹配进程；运行路径、版本、SHA-256及已加载Qt版本均未知，没有为核对启动程序。未查询订阅/配置/内核认证信息。
- 历史公开GUI夹具仅约508KB、56节点、12,001内联规则；76.45MB专用峰、180.10MB工作集、228.35MB私有提交只属旧包，不是本轮或用户场景结果。相关新代码的内存/C++/GUI验收NotRun，旧资源结论对新代码Stale。

## 2. P0-A：仅附着采样（实现与独立脚本验证完成）

- 新增[只读入口](../../../bench/attach_memory_windows.ps1)，复用[Windows三指标](../../../bench/process_memory.ps1)。显式客户端PID列表（含必要辅助服务），内核PID另列；每PID记录专用工作集/工作集/私有提交、采样轮次。按同轮求客户端合计，不相加不同PID各自独立峰值；不自动推断进程组完整性、不输出预算Pass。
- 仅使用Windows查询权限，1—30秒有界采样，默认10秒/100ms；不启动、停止、暂停、注入、改配置或裁剪工作集，不记录命令行/订阅信息。退出/身份变化/访问失败报告Incomplete或拒绝，不以零代替未知。
- 原[运行测量入口](../../../bench/measure_runtime_windows.ps1)会自行启动并最终清理探针，不用于附着用户实例，本轮未执行。
- 依赖PowerShell 7.2+、Windows原生计数API及独立辅助类型加载（不是CMake/产品构建）。[独立测试](../../../tests/scripts/attach_memory_test.ps1)只采样当前测试PowerShell进程，无产品/网络/订阅依赖。实际执行无配置PowerShell脚本命令，退出0，3项通过：自采样三指标及轮次、重复角色PID拒绝、不存在PID拒绝；测试进程仍在运行，临时证据已清理。这不是客户端或用户场景实测。
- 用法及进程组/内核重启边界见[Windows指南](../../baseline/windows_user_guide.md)。

## 3. P0-B/C：有界读取与Geo安全流式提交（实现完成，验证NotRun）

- P0-B：[共享读取器](../../../src/infra/io/bounded_read.h)用64KiB栈缓冲、当前可读量、累计限额+1；支持短读循环，错误/可读却零进展拒绝。文件要求从起点读取、非顺序设备、实际字节等于前后长度且无文件错误。覆盖内核API/流、服务客户端及服务端、管道HTTP、备份恢复、存储/本地导入、订阅/provider与网站测试。原策略/超时/代际不扩大；provider完成尾读另补单项4MiB检查。新增[行为测试源](../../../tests/unit/bounded_read_test.cpp)，覆盖小请求/短读/精确及+1/失败/零进展/空/文件短读与长度增减；已登记独立Core-only测试目标，未配置/编译/运行，不用正则冒充测试。
- P0-C：Geo流式提交实现完成，C++/GUI/网络/内存验证NotRun。[专用写入器](../../../src/infra/storage/geo_resource_writer.cpp)流式写私有临时文件并计算SHA-256，累计32MiB严格拒绝，成功时再次分块复制/摘要复核，经共享路径/ACL、flush/Windows同步、禁止直接写回退的原子提交落盘。普通配置16MiB未扩大。取消/错误显式删除临时文件，析构另有自动删除；存储删除失败返回错误，取消/迟到路径不覆盖新操作（仍有RAII重试清理）。Geo下载保留HTTPS/重定向/15秒传输与30秒总截止，回调核对generation；两个文件及官方-t通过后才切换快照。普通订阅更新不一定执行此路径。中途已经原子提交但后续失败的无引用新文件保留，不清旧资源，本轮不新增垃圾回收。动态源仍为既有官方latest，计算摘要不是签名认证，未更改随包冻结基线。
- Geo校验/运行复制也采用共享分块与累计32MiB、前后长度/摘要校验，禁止直接写回退，不只依赖打开时的长度。
- 新增[Geo行为测试源](../../../tests/unit/geo_resource_writer_test.cpp)：小块/摘要、原子成功、不可变重复保留、取消/迟到/析构清理、空输入、不安全名称、暂存变更、32MiB精确与+1、普通16MiB拒绝、只读转变；已登记，未编译/运行。完整异步超时/取消、ACL与真实GUI资源曲线仍须后续产品验证，不能用源码匹配替代。
- P0子任务未实施P1/P2/P3；后续P1-A独立授权结果见第7节，不得从缓冲修正推断解析图、模型/页面常驻或长期内存已解决。

## 4. P0交付时的实际改动位置（历史切片）

下表行号对应P0交付时的工作区，后续P1-A已移动部分行号；P1-A当前接口见第10节，接续以实际文件为准。

| 文件 | 关键行 | 改动 |
|---|---:|---|
| [共享读取器](../../../src/infra/io/bounded_read.h) | 18、42、53 | 分块消费、追加缓冲、严格文件长度读取 |
| [内核会话](../../../src/infra/core/engine_session.cpp) | 134、242、303 | 服务配置文件、流与API响应 |
| [服务客户端](../../../src/infra/core/service_client.cpp) | 15、68 | 管道缓冲与3MiB响应累计 |
| [管道HTTP](../../../src/infra/core/pipe_http.cpp) | 10、60 | 流/HTTP请求响应分块 |
| [服务端](../../../src/platform/windows/service_host.cpp) | 203 | 24MiB请求累计，认证/重放/所有权不变 |
| [运行门面](../../../src/application/desktop_runtime.cpp) | 639、705、2129 | 本地/订阅/网站响应读取 |
| [provider](../../../src/application/desktop_providers.cpp) | 26、61 | 单项4MiB、累计16MiB与尾读拒绝 |
| [备份恢复](../../../src/application/desktop_backup.cpp) | 63、78 | 48MiB文件读取，无策略上限一次申请 |
| [Geo下载](../../../src/application/desktop_resources.cpp) | 13、30、64、82 | 暂存生命周期、generation、取消/清理错误码日志 |
| [Geo写入器声明](../../../src/infra/storage/geo_resource_writer.h) / [实现](../../../src/infra/storage/geo_resource_writer.cpp) | 15 / 22、39、54 | 专用32MiB、摘要、清理与原子提交 |
| [仓库声明](../../../src/infra/storage/json_store.h) / [实现](../../../src/infra/storage/json_store.cpp) | 28 / 149、189、202、225、241 | 共享安全路径/ACL/同步边界；普通16MiB不变 |
| [Geo校验复制](../../../src/infra/config/geo_resources.cpp) | 76、102 | 严格累计/长度、摘要与原子文件 |
| [有界读取测试源](../../../tests/unit/bounded_read_test.cpp) / [Geo测试源](../../../tests/unit/geo_resource_writer_test.cpp) | 1 / 1 | 两组未编译/运行的C++行为测试 |
| [测试登记](../../../CMakeLists.txt) | 53、192、197 | 新增源与两个现有测试体系目标，未执行CMake |
| [附着脚本](../../../bench/attach_memory_windows.ps1) / [指标桥](../../../bench/process_memory.ps1) | 23、33 / 37 | 查询权限、同轮PID角色和三指标 |
| [独立脚本测试](../../../tests/scripts/attach_memory_test.ps1) | 1 | 自采样及错误PID拒绝 |

同步文档：[设计07](../../design/07-订阅与资源更新.md)、[设计13](../../design/13-性能预算与基准方案.md)、[合并设计](../../design/QtQuick-Mihomo-完整设计文档.md)、[Windows指南](../../baseline/windows_user_guide.md)、[入口](../README.md)、[任务](../TASKS.md)、[清单](../CHECKLISTS.md)、[日志](../DEVLOG.md)及本报告。每个独立切片完成先同步后继续。

## 5. P0执行命令与真实结果（历史切片）

工具环境：PowerShell7.6.6（最低7.2，WindowsApps命令入口）、Qt Creator随附clang-format21.1.2；工具脚本位置见下表。格式器仅整理/检查文本；独立指标桥加载不是产品构建。所有检查从仓库根目录执行。

| 命令 | 实际结果 | 证据 |
|---|---|---|
| git status --short / git diff --stat / git diff | 开始干净；复核增量，无提交 | 本报告工作区说明 |
| 无配置PowerShell只读查询客户端/服务名称、路径/版本/摘要/Qt模块 | 无匹配客户端，身份未知；未读取命令行 | 本报告第1节 |
| pwsh -NoProfile -File [独立脚本测试](../../../tests/scripts/attach_memory_test.ps1) | 退出0，3项通过；最终带轮次版本复测通过 | [日志](client_memory_evidence/attach_memory_test.log) |
| clang-format -i / --dry-run --Werror（第4节16个受影响C++文件） | 退出0；未编译 | [格式/语法日志](client_memory_evidence/static_checks.log) |
| PowerShell标准解析器检查指标桥/附着入口/测试脚本 | 3个脚本通过 | [日志](client_memory_evidence/static_checks.log) |
| pwsh -NoProfile -File [设计合并脚本](../../../scripts/build_design_docs.ps1)，再加 -Check | 均退出0，19分册链接/锚点/合并一致 | [生成](client_memory_evidence/design_generate.log)、[核对](client_memory_evidence/design_check.log) |
| pwsh -NoProfile -File [源码布局检查](../../../scripts/check_source_layout.ps1) | 退出0，36目录/141文件，104产品源码 | [日志](client_memory_evidence/source_layout.log) |
| pwsh -NoProfile -File [进度检查](../../../scripts/check_progress.ps1) | **退出1 / Fail**；仅既有历史包链接本地缺失，没有为此创建文件或构建 | [日志](client_memory_evidence/progress.log) |
| git diff --check | 退出0，仅LF→CRLF提示，无空白错误 | [日志](client_memory_evidence/diff_check.log) |

已知失败是[历史内存报告](20261025_proxy_memory_fixes.md)中的旧包可执行链接本地缺失；本轮未修改该历史报告，也未伪造产物。一次补丁因格式化后的上下文不匹配而未应用，重读后重新成功应用；未发生部分覆盖。一次工作区计数命令误将问号作通配符，未用于结论，最终按字面前缀重新核对。

最终台账收尾后再次核对：进度仍仅上述1项历史链接Fail，MVP Pass=0/16、G2/G3 NotRun；设计-Check、源码布局、差异检查均退出0，工作区按字面状态前缀核对为21修改/15未跟踪，共36文件。所有产品NotRun边界保持不变。

**未运行：** CMake配置/生成/编译、两个新C++测试及既有storage/contracts/runtime/recovery等、GUI、真实Geo/订阅网络、服务/TUN/ACL产品运行、内存与100MB验收、打包。编辑器包含路径诊断未通过配置或编译确认，不当作构建结果。受影响MVP-05/06对当前补丁标Stale，旧报告的旧版通过事实保留；G2/G3仍NotRun，仓库任务计数不变。

## 6. P0交付工作区与后续接续（历史切片）

- P0交付时21个已跟踪文件修改、15个未跟踪文件（5个C++/头/测试源、2个脚本、1个报告、7份真实检查日志），共36文件；P0开始时无已有改动，未提交、未创建远端事项。没有构建/部署产物或用户订阅数据。P1-A保留并续改这些文件，最终工作区见第12节。
- 共享分块回调只借用当前块，不能跨异步保存视图；累计计数由调用者持有，取消/超时/generation仍由宿主控制。缓冲是限额内的完整正文，不是流式JSON/YAML解析；备份仍有JSON/Base64完整表示，P0没有解决解析图/容器常驻/多份语义产物。
- Geo写入器仅供同一worker；仓库须长于写入器。接续不要将专用32MiB能力推广成任意大配置写入。相同摘要目标已存在仍按原不可变写入拒绝，不新增去重复用策略。
- Geo第二遍复制/摘要增加有界本地IO，临时与目标写入可能同时占磁盘；它不保留整份Geo内存。下载摘要不等于上游签名；两资源未全部通过或官方-t失败时不切换旧快照。已提交但无引用资源不做本轮垃圾回收；进程强杀时RAII不能保证临时文件清理，跨启动清理留给对应资源功能。
- 后续P1/P2/P3按父任务既定分析单独授权实施；先保留本P0接口/安全边界，允许构建时再做新测试与受影响回归、核对新二进制路径/摘要/Qt，然后一次用户实际更新短测。100MB只比较同轮客户端/必要服务专用工作集，另保留工作集/私有提交及内核；不得沿用旧公开夹具值宣称本轮达标。

## 7. P1-A：拆分生产传输（实现/Review，验证NotRun）

- 已核对最新四入口、编码规范、设计02/03/08/09/13及原P0差异；原21修改/15未跟踪保留。禁止配置/生成/编译/打包、旧产品探针与用户实例操作；编辑器Qt包含路径诊断不是编译结果。
- DesktopRuntime的changed现在是紧凑可靠控制状态，不含大列表/高频样本；snapshot继续提供原完整诊断字段，定时publish不调用它。首页出口投影和选中名称按源变化更新，避免每秒遍历组/配置。独立RuntimeUpdate只包含变化的完整通道，worker零间隔单次Timer合并同轮发布；未变JSON共享值不展开成QVariant树。
- SettingsService将可覆盖通道经LatestMailbox直接写入受锁值状态，至多一个待处理GUI唤醒、各通道一个最新完整值。新generation丢旧通道，旧generation/冲突epoch拒绝；不是增量补丁丢基线。命令、紧凑控制和Operation完成另走可靠通道。邮箱关闭清值并断开receiver，投递与关闭共锁；QObject事件context撤销迟到唤醒，worker屏障/退出后销毁业务对象。不增加线程池/分析器。
- 日志增加单调localSeq（清空不复用）；连接/规则/provider API回调增加epoch复核，原EngineSession安全校验保留。未改provider下载/解析架构或缓存释放策略。
- 新增[邮箱测试源](../../../tests/unit/latest_mailbox_test.cpp)，登记latest_mailbox现有测试体系目标：GUI阻塞期间200次遥测→单次最新值、200个可靠控制按序保留、通道合并、排空后再投递、新旧代次/冲突epoch、关闭/销毁与并发publisher。仅写入，未编译运行，NotRun。
- 本切片完成先同步设计02/03和四入口/日志/本报告；下一项完成GUI数据消费者、增量模型和页面。此时消费者尚未迁移，不能作为可运行产品交付或宣称P1-A全部完成。

## 8. P1-A：GUI独立消费者与增量列表（实现/Review，验证NotRun）

- UiState公开runtime控制、telemetry、profiles/groups/nodes/delays独立通知及四个live模型；未变化JSON通道先比较，避免重复转换；样本不触发runtime/profiles/groups通知。新generation清会话行与曲线、拒绝旧投递，不因epoch变化通知未变profiles/组。显式diagnosticSnapshot供既有GUI订阅探针使用；不绑定生产属性、不从普通定时更新隐式生成完整树。
- RuntimeRows按连接epoch/id、provider kind/name、日志localSeq、规则type/payload/proxy语义元组＋同语义出现序号识别行，哈希核对、连续范围增删、相邻同roles合并通知，无正常reset。C++代理保留150ms文本防抖、稳定次级键、速率1000ms合并重排、文本/数值/日志来源级别过滤；可见delegate按entry role单行转换，批量关闭最多500个Identifier直接捕获。规则源顺序单独维护，支持中间增删/重排；完全相同重复规则无法区分物理身份，不用于控制。
- 连接/规则/日志及通用RuntimeList已接新模型；日志暂停冻结GUI行/停止页面需求，仅保存最新有界JSON待恢复，清空同时清冻结行，组件自有Timer合并跟随且用户滚离尾部不强拉。provider动作、关闭确认和捕获epoch/id、空态、ListView复用保留；首页/代理页移到独立样本/组/延迟，不实施卡片虚拟化或隐藏缓存释放。
- 新增runtime_publication、runtime_list_model与ui_runtime_lists测试源/目标；扩展既有ui_state。覆盖发布同轮合并/不变无通知、字段roles不reset/身份增删、过滤排序/捕获集合、规则顺序/provider身份、日志暂停跟随、旧代次/销毁重建；邮箱测试另含跨线程与可靠控制。所有C++/GUI均未配置、编译或运行，NotRun，不能用源码匹配替代运行证据。
- 设计08/09/13、任务/清单/入口及本报告同步后，下一项静态/格式/文档检查。传输切片设计生成及-Check已使用PowerShell7通过；首次误用Windows PowerShell导致UTF-8解析失败，已改用P0同一PowerShell7入口，不修改脚本编码绕过；一次多文件补丁上下文不匹配未应用，核对后重试，无部分覆盖。
- 静态复核修正规则槽位身份为语义身份与独立源顺序，新增持久索引跨中间插入/删除/重排与重复规则测试源；日志source/level变化即使拼接检索文本不变也刷新精确过滤，并补测试源。此为源码审查修正，不是功能测试结果；实现/Review，验证NotRun。
- 隔离GUI测试的代次断言前显式清理前序连接筛选，避免保留的alpha筛选将新epoch夹具隐藏而混淆代次检查；产品筛选保留语义不变。测试源仍NotRun。
- 工程登记静态复核补BUILD_TESTING下的app私有包含目录，使新增集成GUI测试源可以解析ApplicationHost头；未执行CMake或编译确认。

## 9. P1-A实际检查与未运行项

以下仅为文本/文档检查，不是C++语法编译、Qt类型登记、功能/并发行为或产品内存证据。使用既有PowerShell7.6.6、clang-format21.1.2、qmlformat6.9.0；后者解析通过不等于冻结最低Qt6.8运行兼容通过。

| 检查 | 真实结果 | 原始证据 |
|---|---|---|
| 19个新增/受影响C++文件格式整理及dry-run/Werror | 退出0；GUI夹具收尾后19文件再次退出0 | [格式与QML解析](client_memory_evidence/p1a_static.log) |
| 通用列表、连接/规则/日志/首页/代理页共6个QML文本解析 | 各退出0，仅qmlformat解析；未运行qmllint/产品生成 | [日志](client_memory_evidence/p1a_static.log) |
| 设计合并生成，再执行-Check | 各退出0，19分册/合并内容/链接/锚点一致 | [生成](client_memory_evidence/p1a_design_generate.log)、[核对](client_memory_evidence/p1a_design_check.log) |
| 源码目录/文件布局 | 退出0，36目录/151文件；脚本报告114个产品源码检查项 | [布局](client_memory_evidence/p1a_source_layout.log) |
| 进度脚本（独立PowerShell7进程） | **退出1 / Fail**；仅1项既有历史包可执行链接本地缺失，MVP Pass=0/16、G2/G3 NotRun、任务计数不变 | [进度](client_memory_evidence/p1a_progress.log) |
| git diff --check | 退出0；LF→CRLF提示不是空白错误 | [差异](client_memory_evidence/p1a_diff_check.log) |
| git status/diff/stat只读核对 | 39个已跟踪修改、31个未跟踪，共70文件；无删除/提交/产物 | 第12节 |

首轮组合命令中的进度脚本抛出异常，组合命令退出1，未执行后续diff；随后用独立进程保留进度退出1和错误全文，再单独完成diff退出0。没有将外层命令退出0改称进度通过。历史缺失链接仍位于20261025_proxy_memory_fixes报告，未改该报告、伪造可执行文件或为此构建。

**NotRun：** CMake配置/生成、moc/qmltypes/qmlcachegen/lrelease、产品编译/打包、latest_mailbox/runtime_publication/runtime_list_model/ui_runtime_lists及既有ui_state、受影响runtime/recovery/GUI回归、真实订阅/内核/系统代理/服务/TUN、资源曲线/100MB与长期泄漏验收。测试仅有源和工程登记；编辑器缺Qt包含路径等诊断未经编译确认。P0独立脚本3项通过仅属第5节历史，不是P1-A产品证据。未启动旧二进制或操作用户实例。

## 10. P1-A当前接口、完整数据路径与生命周期

| 入口 | 当前关键行 | 用途 |
|---|---:|---|
| [完整通道值类型](../../../src/application/runtime_update.h) | 8 | generation/epoch/values；键缺席=未变化，空集合=显式清空，每个值均是完整通道状态 |
| [运行门面](../../../src/application/desktop_runtime.cpp) | 432、440、489、511、543、2096 | 直接取选择generation、紧凑控制、显式完整诊断、同轮合并、仅变化完整通道、可靠Operation完成后发布控制 |
| [邮箱](../../../src/infra/storage/latest_mailbox.cpp) | 20、41、47 | 任意生产线程写值、GUI关闭和排空；无模型跨线程调用 |
| [存储服务](../../../src/infra/storage/settings_service.cpp) | 14、30—36、75 | 控制/Operation可靠转发，dataPublished直写邮箱；析构先close再屏障/quit/wait |
| [GUI状态](../../../src/presentation/ui_state.cpp) | 44、67、78、127、134 | 代次拒绝、控制与通道独立应用、暂停/恢复、仅显式诊断组装 |
| [行存储/过滤排序](../../../src/presentation/runtime_list_model.cpp) | 52、72、110、223、266、285、305、313 | 按需单行角色展开、身份、差异增删更新、C++代理、合并重排与有限ID捕获 |
| [通用列表](../../../qml/components/runtime_list.qml) | 10、21—25、51 | 模型接口、150ms防抖/跟随合并Timer、虚拟化与复用 |
| [连接页](../../../qml/features/connections/connections_page.qml) / [规则页](../../../qml/features/rules/rules_page.qml) / [日志页](../../../qml/features/logs/logs_page.qml) | 43—86 / 49—59 / 11—17、63—70 | 排序及捕获确认、provider动作、暂停/跟随与需求生命周期 |
| [探针兼容](../../../app/main.cpp) / [隔离UI测试源](../../../tests/integration/runtime_ui_probe.cpp) | 203 / 24 | 原订阅探针显式取诊断；新UI夹具只注入合成数据，不创建SettingsService |
| [工程登记](../../../CMakeLists.txt) | 84—89、174—179、242—277、294—298 | 传输/模型源码、三单测与既有ui_state、新隔离GUI目标（均未配置/运行） |

完整生产路径：worker内核/存储回调更新JSON集合→publish同步比较并可靠排队紧凑控制→零间隔单次Timer的flushData比较publishedData基线→RuntimeUpdate仅带变化的完整samples/profiles/groups/nodes/delays/connections/rules/providers/logs通道→DirectConnection调用邮箱publisher，仅更新锁保护值→最多一个context-bound GUI唤醒→SettingsService.dataChanged→UiState先校验generation/epoch、再按通道同值跳过→小样本/配置/组等分别转换通知，四大列表进入RuntimeRows→C++代理筛选排序→可见delegate请求entry时单行转换。普通生产定时链路不调用完整snapshot或GUI diagnosticSnapshot；显式诊断/导出和测试调用仍可取完整表示。

可靠路径单独保留：GUI命令排到worker；changed中的控制/错误/Operation状态及operationCompleted逐项排回GUI，不放进覆盖邮箱。邮箱值合并只用于可覆盖的完整通道，不合并操作完成事件。

生命周期约束：业务集合和publicationTimer只在worker线程；四个行存储/代理及所有begin/end/通知只在GUI线程。publisher捕获共享State，不捕获服务/模型生命周期；receiver指针、enqueue和close共锁，关闭后的publisher安全no-op；排队回调带QObject context，销毁撤销迟到事件。drain锁内交换值/复位pending，锁外通知，允许消费者销毁receiver。SettingsService析构先断邮箱，再worker屏障、quit/wait、worker内销毁runtime/store；UiState的settings最后声明、最先析构，worker结束后才销毁模型。新generation清会话行/曲线，旧generation或同代不同epoch拒绝；API连接/规则/provider回调再核对epoch。上述为实现与静态审查说明，并发/销毁运行测试仍NotRun。

## 11. P1-B接续清理入口（仅交接，本轮未实现）

| 层 | 需接续的入口/状态 | 清理与防迟到要求 |
|---|---|---|
| 页面/GUI需求 | [UiState](../../../src/presentation/ui_state.cpp) 的setDemand(350)、setUiVisible(309)、updateTimer(365)；各页Component析构/需求释放 | 依据最后一个需求token注销，不因单页离开误清仍有消费者的流；不改授权外的隐藏策略。rules/providers目前不在允许stream列表内，需先扩展显式请求/需求契约，不能假定已有对应stream。 |
| GUI缓存/模型 | 同文件applyData(78)、acceptGeneration(44)、setLogsPaused(127)；[RuntimeRows.clear](../../../src/presentation/runtime_list_model.cpp) (107) | 清目标行与appliedData相应通道、pendingLogs、曲线/必要样本；不要误清profiles/业务控制。给代理补充排序Timer停止/视图失效入口；暂停冻结不是无需求清缓存，恢复语义要分别定义。 |
| worker业务缓存 | [DesktopRuntime.setDemands/pollFeatures](../../../src/application/desktop_features.cpp) (67/102)；connections/rules/providers/logs、featureRequest、connectionSampleMs等 | 先使该流请求代次失效，再取消/注销网络流并按策略清缓存；当前仅epoch不够阻挡同epoch内已取消需求的迟到API回调。日志App级留存与页面流分别决定，不盲目清日志。 |
| worker发布基线 | [publish/flushData](../../../src/application/desktop_runtime.cpp) (511/543) 的publishedData、summaryGroups/summaryProfiles及业务大集合 | 释放目标通道共享基线/派生缓存；恢复需求必须重新发送完整基线，不能因publishedData仍“相同”而不发布。group/profile缓存的释放须保留控制摘要一致性与仍在使用的数据。 |
| 邮箱/GUI代次 | [LatestMailbox](../../../src/infra/storage/latest_mailbox.cpp) (20/41/47)、UiState.acceptGeneration/applyData | 设计需求/通道generation或显式invalidate协议，同步使worker在飞回调、邮箱待投递和GUI旧值失效，防止清空后被旧数据复活。现close是整个receiver的终止接口，不能拿来替代长期服务存活期间的逐流注销；全局epoch变化也不能替代逐流代次。 |

不切断可靠控制/Operation通道，不把新会话/新需求数据当作旧缓存丢弃。后续测试应覆盖最后token释放、同epoch取消后迟到、邮箱已有待投递、清空后重新完整发布、日志暂停与恢复；本轮仅交接这些入口，没有实施P1-B或宣称释放后内存回落。

## 12. P1-A范围结论与最终工作区

- P1-A所列传输、合并、增量模型、QML消费者与测试源/登记已实现，状态**Review**；未发现另有已知范围内实现待办，但编译、Qt登记、线程/UI功能及产品验收均未运行，不能标产品Done/Pass。具体语义限制：完全相同重复规则仅能按出现序号区分；模型收到完整JSON仍需线性核对，不是零扫描；代理和可见delegate仍有必要角色转换。
- 仍保留worker业务集合、publishedData、出口投影输入缓存、GUI appliedData与行JSON、暂停日志最新值等（Qt JSON隐式共享）；本轮不清无需求缓存、不做provider解析优化、不虚拟化订阅卡片、不更改隐藏策略。P1-B/P2/P3需另行授权，不能从本轮源码推断具体MB收益、100MB达标或长期泄漏已解决。
- 最后按git状态字面前缀核对：**39个已跟踪修改＋31个未跟踪＝70文件**，无删除；其中原P0的21修改/15未跟踪全保留，P1-A另使18个已跟踪路径变更、新增10个源码/头/测试文件和6份工程检查日志，并续改P0共享接口/报告。未reset/提交/创建远端事项，无构建/部署产物及用户配置/订阅数据。
- 新增10个源码/头/测试文件：runtime_update头、latest_mailbox两文件、runtime_list_model两文件、latest_mailbox/runtime_list_model/runtime_publication三个单测源、runtime_ui_probe两文件；P0有界读取/Geo写入器/附着脚本及测试未移除。现有Qt/源码路径编辑器诊断留待获准构建后核对，不额外运行产品生成来消除诊断。
- 已同步设计02/03/08/09/13与合并设计、四入口/任务/清单和本报告；历史P0第1—6节按当时事实保存。MVP Pass=0/16，MVP-05/06保持Stale、G2/G3 NotRun，仓库任务计数不变。下一步由父任务决定授权的P1-B或新代码构建/定向回归；不拿旧包补本轮证据。

## 13. P1-B协议与逐流释放（实现/Review，验证NotRun）

- 已核对四入口、规范、git status/diff及第11节清理入口，保留全部P0/P1-A。严格不构建，不运行CMake/产品生成/编译/打包或旧二进制，不请求真实订阅、不操作用户实例/代理/TUN。产品C++/GUI/并发/内存均NotRun；编辑器包含路径诊断不是编译结果。
- 新RuntimeDemands按实例合并消费者，逐流generation在首入、末退与清空递增。GUI先同步更新邮箱的精确代次/active状态并清自己，再将完整需求状态排给worker；worker拒绝倒退需求，使旧读取序号失效后清缓存和发布比较基线。RuntimeUpdate附需求代次及逐通道invalidated，不全局关闭邮箱。失效通知可属于当前inactive代次，普通值必须精确代次且active；会话publication generation/epoch仍独立校验。
- 读取请求联合epoch、需求generation、operationGeneration和单调请求序号；旧回调不能清新请求在飞标记。连接/规则/provider两阶段读取和服务资源均覆盖；服务日志/流量轮询增加流启停代次，防止同session退进后旧IPC回包复活。无需求的普通API回复仍按既有限额/超时完成并被拒绝，不声称物理取消服务端请求。
- 确切释放：worker connections/rules/providers及provider中间元数据、连接采样时钟/stale/sample、页面Core日志与丢弃计数、traffic/memory样本；publishedData目标键；邮箱目标待值/失效值；GUI appliedData目标键、行JSON/检索索引、暂停pendingLogs、曲线及目标遥测，停止代理延迟排序并失效视图索引。释放引用不保证分配器/OS立刻归还物理页，无MB收益结论。
- traffic/resources/connection_status/log_status拆成关联需求通道，samples仅uptime，GUI仍统一投影telemetry。profiles/groups/nodes/delays及出口摘要是业务选择/控制数据，本切片不回收；独立App诊断小环最多100条、消息2048字符，Core页面环仍最多1000条。已确认连接关闭事务只留最多500个id，不因页面清缓存取消；后台订阅、核心心跳/恢复、可靠控制/Operation不停止。
- 需求重新获取清比较基线并完整发送；规则/provider在Ready且不busy时按需读取，失败等待手动刷新/新需求/新会话，不每秒重试失败。显式snapshot/diagnosticSnapshot只看当前已持有状态，不自动申领大列表，不加测试旁路。
- 本切片先同步设计03/13与台账/日志；下一项页面页签/日志生命周期及测试源。实际静态检查、证据和最终行号待后续记录，不标Pass。

## 14. P1-B页面/源测试切片（实现/Review，运行NotRun）

- 三个大列表页面各生成唯一实例需求键，旧实例销毁不注销新实例。规则页只持有所选rules或providers需求，页签切换通过同一实例键替换；隐藏/销毁释放，Ready后worker按需首取，原手动刷新保留。没有调整订阅卡片或窗口卸载策略。
- 日志拆分可见保留logs与接收log_feed；暂停只释放本页接收，其他接收消费者不受影响，GUI当前共享模型冻结，继续/清空/跟随与过滤保留。最后logs消费者离开或GUI隐藏，worker/发布/邮箱/GUI/冻结/待恢复缓存均释放。App基本诊断最多100条独立保留，导出无需求时仅含App小环；Core页面环最多1000条、消息2048字符、来源/级别32字符。当前共享日志模型不提供每嵌入视图独立暂停模型，这不是本轮新增功能。
- 既有六组测试源扩展，无新增测试专用生产旁路、无CMake执行：latest_mailbox覆盖逐流撤销、保留其他通道、排队旧值与重进旧publisher；runtime_publication覆盖实际生产票据谓词的同session/操作/请求/会话拒旧、worker释放/失效/同值完整重送及日志1000/100条界；runtime_list_model覆盖持久索引失效、停止延迟排序与保留过滤偏好；ui_state覆盖共享消费者、最后释放/改流、所有模型/JSON基线/暂停副本/遥测清理、复进/清空拒晚到；runtime_ui_probe覆盖实际页签/可见变化/销毁、日志暂停接收与保留/跟随及旧投递；runtime集成源显式申请规则需求并核对释放/同值重新获取。全部仅源码，未编译运行。
- 显式诊断新增稀疏dataLifecycle计数/通道键（非正文），便于核对发布/应用基线和在飞读取；不挂在生产属性或定时控制。测试读取当前生产需求代次构造合成消息，必须先真实申领需求；诊断快照本身不申请数据。
- 设计03/13首轮生成与-Check各退出1，仅新增文件链接含行号而脚本未支持；改为说明性无行号链接后重试各退出0，原失败日志保留于client_memory_evidence。一次组合文档补丁因指南上下文不匹配未应用，重试成功；不是产品失败。此切片已同步设计09、Windows指南、四入口与日志，下一项允许的静态检查。

## 15. P1-B静态复核：服务日志撤销（实现/Review，运行NotRun）

- 首轮允许检查已实际执行：20个C++格式、3页QML格式/6份QML文本解析、设计生成/-Check、源码布局与diff退出0；进度退出1，仅历史包链接缺失。日志分别为本证据目录的p1b_static、p1b_design_generate/check、p1b_source_layout、p1b_progress和p1b_diff_check，不是产品Pass。
- 进一步沿服务路径发现原log_demand忽略失败；客户端4个请求上限或服务核心busy可拒绝关闭，而本地已移除需求，原路径不保证再发关闭。现增加生产ServiceLogDemand确认状态：至多一个日志开关请求在飞，失败保持未确认，沿既有一秒轮询/后续需求调用重试，不增加独立网络重试Timer。
- 日志需求代次在服务端旧enable已确认或仍在飞时发生变化，新需求必须确认disable（服务端删除流、清200条环和丢弃计数）后才能enable/读取；旧完成不确认新需求，旧正文回包仍按流/会话代次拒绝。同步回调先核对engine session，stop重置确认状态。不改变IPC消息字段或关闭控制器/核心/心跳；服务端disable仍经过身份/会话验证，但不再受核心busy/未Ready挡住。
- 既有runtime_publication源补同步状态机的单在飞、失败重试、退进期间旧disable/enable确认及先清再启行为；没有启动任何服务或测试二进制，完整IPC/服务行为仍NotRun。服务端断流提示移到真正读取日志时添加，避免无关命令反复向闲置环添加提示。
- 已先同步设计09、指南、入口/台账/清单/日志；此修正后的格式、设计生成/-Check和收尾检查待运行，不能复用此前检查冒充新结果。

## 16. P1-B真实检查与未运行项

工具为PowerShell 7.6.6、clang-format 21.1.2、qmlformat 6.9.0。这里只运行格式、文本解析和仓库文档检查，未执行产品配置/生成/编译、Qt代码生成、部署或旧包。本节为第15节修正之后的结果，早期失败日志保留。

| 检查 | 实际范围/命令说明 | 真实结果与原始证据 |
|---|---|---|
| C++格式 | 21个受影响源码/头/测试，先格式化，再dry-run及Werror；含新增服务日志同步修改 | 两步均退出0；[最终格式日志](client_memory_evidence/p1b_final_static.log) |
| QML文本解析 | 6份：共享运行列表、连接/规则/日志、首页/代理页；三个改动页另执行格式化 | 全部退出0；[解析日志](client_memory_evidence/p1b_static.log)，后续仅改C++/文档，复用有效证据 |
| 设计合并及校验 | [设计合并脚本](../../../scripts/build_design_docs.ps1)及其Check模式，19章和本地链接 | 各退出0；[生成日志](client_memory_evidence/p1b_final_design_generate.log)、[校验日志](client_memory_evidence/p1b_final_design_check.log) |
| 源码命名/布局 | [布局脚本](../../../scripts/check_source_layout.ps1)，36目录/151文件，产品源码114项 | 退出0；[布局日志](client_memory_evidence/p1b_final_source_layout.log) |
| 任务/证据进度 | [进度脚本](../../../scripts/check_progress.ps1) | **退出1**，仅历史代理内存修复报告所链旧包本地缺失；[进度日志](client_memory_evidence/p1b_final_progress.log)。未构造产物/修改旧事实绕过 |
| tracked差异检查 | git diff的check模式；Git另提示已有LF/CRLF转换策略，不是编译诊断 | 退出0；[差异日志](client_memory_evidence/p1b_final_diff_check.log) |
| 工作区/位置核对 | git状态、统计、HEAD及文本位置检索；不编译 | 退出0；[工作区日志](client_memory_evidence/p1b_final_worktree.log)，逐项检查码见[结果表](client_memory_evidence/p1b_final_check_results.log) |

组合命令外层正常结束不等于所有子检查成功；进度子进程明确为1。首轮设计因新增链接行号失败已保留于协议检查日志，修复后才生成成功；没有隐藏失败、替换检查器或新增产品Pass。

QML只用6.9工具进行文本解析，不是qmllint/Qt类型检查，不能证明冻结最低Qt6.8兼容、绑定有效、实际卸载时序或GUI功能。编辑器缺Qt包含路径的提示也不是本轮编译结果。六组C++/GUI测试源、完整IPC/权限服务、并发/迟到时序、控制确认、真实订阅/系统代理/TUN、所有产品内存/工作集和长期泄漏验证均**NotRun**；没有启动旧二进制补证。G2/G3保持NotRun，MVP-05/06保持Stale，MVP Pass仍0/16，仓库完整P3不标Done。

## 17. P1-B交付接口、工作区及接续边界

以下行号是当前源码文本位置，列在说明中而不放本地链接目标；不是编译或运行验收。

| 交接入口 | 当前位置与职责 |
|---|---|
| [需求/读取/发布协议](../../../src/application/runtime_update.h) | 第12/55/68行：逐流active和代次、联合读取票据、完整值及失效消息 |
| [GUI需求和投递](../../../src/presentation/ui_state.cpp) | 第70/120行：拒旧/清目标通道；第360/401/404/417行：可见性、实例键、消费者合并与同步注销；第558行：清日志轮换代次 |
| [服务需求桥](../../../src/infra/storage/settings_service.cpp)和[邮箱](../../../src/infra/storage/latest_mailbox.cpp) | 第102行先GUI同步邮箱fence再排worker；邮箱第21/64行按通道代次gate及撤销待值 |
| [worker页面数据](../../../src/application/desktop_features.cpp) | 第68/76/106行需求/逐流释放；141/146行联合票据；625行规则/provider按需读；767行独立已确认ID事务 |
| [worker发布/会话清理](../../../src/application/desktop_runtime.cpp) | 第564/1797行逐流完整发布/会话清理；515行显式生命周期诊断，不自动申请需求 |
| [模型释放](../../../src/presentation/runtime_list_model.cpp) | 第107/250行释放行容器、停止延迟排序、使代理索引失效，保留有界偏好 |
| [连接页面](../../../qml/features/connections/connections_page.qml)、[规则页面](../../../qml/features/rules/rules_page.qml)、[日志页面](../../../qml/features/logs/logs_page.qml) | 均第12行唯一实例键；规则第26行页签换需；日志第16—17/28行保留/接收双token及销毁 |
| [服务日志确认状态](../../../src/infra/core/engine_session.h)、[引擎同步](../../../src/infra/core/engine_session.cpp)、[服务端释放](../../../src/platform/windows/service_host.cpp) | 第15、242/270、362行；失败重试、代次更换先确认清服务旧环，不停止核心/心跳 |
| [发布测试源](../../../tests/unit/runtime_publication_test.cpp)、[邮箱测试源](../../../tests/unit/latest_mailbox_test.cpp)、[模型测试源](../../../tests/unit/runtime_list_model_test.cpp) | 第23/55/76、80、26行分别覆盖确认重试/票据/worker释放、逐流撤销、行与排序释放；均NotRun |
| [GUI状态测试源](../../../tests/unit/ui_state_test.cpp)、[QML宿主探针](../../../tests/integration/runtime_ui_probe.cpp)、[runtime集成测试源](../../../tests/integration/runtime_test.cpp) | 第99、24、787行起覆盖共享/末退/清空、真实页面申领与迟到、headless显式申领规则；均NotRun |

- 最终基于45229be，**41个tracked修改＋49个untracked＝90文件，无删除**。对照初始39/31共70路径，P0/P1-A路径仍在；本轮使引擎头和既有runtime集成测试另有tracked修改，新增18份文本检查证据，未新增产品源码文件。初始有界读取、Geo写入器、附着脚本及其测试保留。没有reset、提交、远端事项、构建/打包产物或用户配置/订阅数据操作；已有工程/依赖改动不是本轮清理对象。
- **优化P2未实施**：provider下载、解析、受管资源快照和更新控制链路不变；本轮只管理页面汇总元数据，可复用按需读取和可释放中间元数据，不应把下载正文纳入页面publication。**优化P3未实施**：订阅卡片和窗口隐藏卸载设置不变，可在独立授权任务复用typed需求/逐流注销，不能据本轮将完整仓库P3标Done。
- 保留边界：profiles/groups/nodes/delays及出口摘要为业务选择/控制输入，不按页回收；App诊断独立100条；已确认连接关闭最多500个ID按事务保留。当前共享日志行模型/暂停状态不是每嵌入视图独立暂停模型；多消费者需求仍合并，其他接收消费者不会因单页暂停而被停止。
- 传输收尾边界：未物理取消所有在飞API/IPC，沿原限额/超时结束后拒旧；权限服务原32项有界请求结果重放缓存也按其传输/会话周期淘汰，不承诺随页面末退同步销毁所有临时JSON。新增确认重试仍需真实IPC时序/失败/断线回归，未改变服务认证、核心状态轮询和恢复策略。释放Qt隐式共享引用不等于分配器/OS立即返页，没有具体MB收益、100MB达标或长期泄漏已解决的结论。
- P1-B代码与测试源已实现，交付为**Review**而非产品Done/Pass。下一步由父任务决定获准构建后的六组定向回归、同会话退进/清空与服务关闭失败测试，再按需做单个短时客户端/辅助服务内存复测；本轮不执行上述运行，也不以旧包替代新代码证据。

## 18. P2-A：provider私有暂存与规则流式展开（实现Review，运行NotRun）

- 独立授权优化P2，非仓库阶段。已读四入口/规范、设计06/07/13、当前源码及git status/diff；保留45229be之上41 tracked修改+49 untracked共90路径。不构建、不执行CMake/产品生成/编译/打包/旧包，不请求真实订阅，不操作用户进程/代理/TUN，不实施优化P3。
- [暂存器](../../../src/infra/storage/provider_stage.h)逐项64KiB、4MiB/16MiB/32项，溢出哨兵不落盘；[仓库固定入口](../../../src/infra/storage/json_store.cpp)共用安全路径/私有父目录/文件ACL，不采纳源path作路径。文件归同一事务RAII，失败/取消释放；[生产回调](../../../src/application/desktop_providers.cpp)仅持弱事务引用并核对上下文与代次，逐项完成后排下一项。保留TLS、拒全部redirect、10秒传输/15秒截止；实际网络/ACL/清理均NotRun。
- [设备流桥](../../../src/infra/io/device_stream.h)64KiB读取，无整份标准字符串副本，检查起点/非顺序/前后长度/实际读取/文件错误；输出写入前检查16MiB。[事件扫描](../../../src/infra/config/config_metadata.cpp)新增标量序列同步回调，主源rules及YAML payload不进入Node图或全部规则vector；文本逐行最多8193字节，规则8192。重复键/别名/多文档/非法标签/嵌套规则拒绝，深度32/单文档100000事件。
- [文件展开器](../../../src/infra/config/config_provider_stream.cpp)在RULE-SET原位置重读文件，保留重复引用、classical/domain/ipcidr/no-resolve、use原序。节点5000/组500/累计成员100000/结构字段累计4MiB、初次主源+provider累计100000事件；扩大成员前检查，展开规则累计20000/输出16MiB追加前检查。受限节点/组图仍含身份必需秘密，但不向UI公开。兼容缓冲API转同一逻辑，生产走文件；最终既有完整schema/官方-t仍需一份≤16MiB字节，不是全链路零拷贝。
- 本切片主源原文暂仍由上下文持有，导入原文/展开/语义生命周期留给下一项P2-B，不把局部流式路径当整笔事务已优化。
- [新增行为测试源](../../../tests/unit/provider_stream_test.cpp)登记覆盖串行/短块、4MiB精确/+1、16MiB累计/+1、32项/重复key、取消/迟到/析构/空清理、多provider/重复引用/顺序/各behavior/no-resolve/use/身份、异常YAML/深度、20000精确/累计+1、输出字节膨胀及失败留旧。仅源码，未编译运行/NotRun，不替代实际网络TLS/redirect、取消时序、ACL、官方-t/GUI/内存。工程静态和设计合并待运行；文档补丁上下文失败已重读重试，无产品结果。

## 19. P2-B：订阅事务缓冲生命周期（实现Review，运行NotRun）

- 主源在provider下载前经JsonStore写不可变候选，释放正文；provider只持文件与受限元数据，完成展开后关闭文件/删除暂存，最终≤16MiB快照读入一次交给完整预检。普通原文/展开快照在预检后清空，官方隔离校验仍保留，没有UI已验证入口。
- [私有事务上下文](../../../src/application/desktop_runtime.h)只持语义产物、偏好、来源/快照引用与摘要、操作代次；回调弱引用联合核对身份/代次。校验回调→导入提交→应用提交→启动及失败恢复分步排队，避免原调用栈同步嵌套；只有内部官方校验成功才能标可信，来源/快照/偏好必须精确匹配。停止/失败清上下文，迟到票据不能重新启动。
- [分块复核](../../../src/infra/storage/json_store.cpp)64KiB累计SHA-256，保留安全路径、16MiB限额、前后长度/读取错误检查。普通同内容更新不再整份读取第二份源；立即应用复核候选文件而不再预检，启动复核artifact并移动语义，冷启动/恢复仍读必要产物。不取消完整性校验，不把摘要当UI可传的验证证明。
- 校验和启动的会话物化字节只在写盘局部作用域存活；启动进程前释放语义。既有预检仍会产生有界规则向量、字段序列化与语义字节，物化仍有短时完整会话产物，节点/组小图和解析器/分配器驻留仍存在，非零拷贝/非峰值实测。
- 新增原文/快照在catalog接管前由私有RAII引用集合尝试移除，provider临时文件归暂存器，失败不触碰旧候选；catalog接管后保留原历史/GC及应用失败补偿策略。OS拒绝删除会告警，不能保证文件一定删除或强杀清理，未新增启动时扫描未知临时文件的机制。
- [摘要单测源](../../../tests/unit/provider_stream_test.cpp)新增跨块、精确16MiB/+1、错摘要/篡改/非法路径/缺文件；[runtime测试源](../../../tests/integration/runtime_test.cpp)扩展同内容provider不重启、变化按原事务应用、坏候选保旧PID/摘要、停止校验/迟到、暂存与语义阶段释放。仅显式诊断snapshot增加稀疏计数，不含正文/秘密、不进入生产控制/数据发布；原P1逐流需求协议不变。全部C++/网络/ACL/官方-t/GUI/内存NotRun。
- 中间真实检查：P2-A的14个C++格式、设计生成/-Check退出0，见[摘要](client_memory_evidence/p2a_results.log)及同目录日志；P2-B修改后15个C++格式化/校验退出0，尚待最终证据。人工回读修复多余括号、provider100条限额旁路、失败busy发布及测试变量名；编辑器缺包含路径的诊断不是构建结果。最终静态检查待下一步，不宣称MB收益/100MB达标/泄漏解决。
- 复核续修：空provider/大注释的重复引用仍可能放大磁盘/解析工作量，新增整次累计128MiB扫描/200000事件（初次100000和单文档100000仍保留），超限失败丢弃输出；新增对应拒绝测试源。显式标签按标量/序列/映射类型白名单，不再只认标准命名空间前缀，并补伪标准标签测试源。可信立即应用同时复核原文/快照；均Review/运行NotRun。解析器仍可为输入限额内单token分配缓存，不能把事件回调8192限制说成解析器从不分配更大token。

## 20. P2最终工程检查与工作区

本节是允许范围的真实检查，不是C++或产品验收。原始结果见[退出码](client_memory_evidence/p2_final_results.log)。

| 检查 | 实际结果 | 原始证据 |
|---|---|---|
| 15个受影响C++/头/测试文件格式化及dry-run | 0 / 0；只检查格式 | [格式日志](client_memory_evidence/p2_final_format.log) |
| 设计文档合并生成、随后-Check | 0 / 0；19章合并/锚点/本地链接一致 | [生成](client_memory_evidence/p2_final_design_generate.log)、[校验](client_memory_evidence/p2_final_design_check.log) |
| 源码命名布局 | 0；36目录/156文件，其中119产品源码 | [布局](client_memory_evidence/p2_final_source_layout.log) |
| 进度/台账/链接 | 1；只有历史20261025_proxy_memory_fixes报告引用本地dist/proxy_memory_fixes/bin/quickclash.exe缺失；没有改旧报告或伪造包 | [进度](client_memory_evidence/p2_final_progress.log) |
| git diff --check | 0；保留Git的LF/CRLF提醒 | [差异](client_memory_evidence/p2_final_diff_check.log) |
| 独立文本复核 | 0；15文件字符串/注释剔除后的括号平衡、登记/私有入口/诊断发布边界文本核对、源码摘要、原改动路径核对 | [可重跑辅助源](client_memory_evidence/p2_static_review.py)、[输出](client_memory_evidence/p2_final_worktree.log) |

- 全部CMake配置/生成/编译、Qt产品生成、打包、C++单元/集成/GUI/网络/ACL/官方内核校验/产品内存均**NotRun**。没有用旧二进制证明补丁，没有访问真实订阅或操作用户进程/系统代理/TUN。工程文本断言不能证明类型正确、链接通过、取消时序正确或内存下降。
- HEAD仍45229be；最终47 tracked修改+67 untracked=114路径、删除0。相对本轮90路径新增6个tracked改动、5个未跟踪源码/测试与13个工程证据文件；原90路径仍存在且有工作区改动。由于没有完整初始字节快照，该核对仅证明路径保留，不声称逐字节比对；本轮没有reset、自动提交或创建远端事项。
- 原P0/P1日志/测试源/需求协议保留。MVP-05/06继续Stale，G2/G3继续NotRun，仓库阶段任务数不变。首次文本输出的中文编码显示异常，辅助程序固定UTF-8后重跑；不改产品源码，旧P1历史日志原样保留。

## 21. P2交接接口、数据流与未验收风险

| 入口 | 本轮职责 |
|---|---|
| [ProviderStage](../../../src/infra/storage/provider_stage.h)、[实现](../../../src/infra/storage/provider_stage.cpp) | 串行受限文件暂存，固定块消费、封口/取消/RAII；文件借用不越过展开调用 |
| [JsonStore](../../../src/infra/storage/json_store.cpp) | 固定私有暂存入口与ACL、安全路径、64KiB流式摘要复核 |
| [设备流](../../../src/infra/io/device_stream.h)、[事件元数据](../../../src/infra/config/config_metadata.cpp) | Qt设备/标准流桥，根标量序列同步sink，单文档/深度/重复键/标签限额 |
| [expandProviderFiles](../../../src/infra/config/config_provider_stream.cpp) | 主源与YAML/text provider规则流式展开、重复引用重读、小图/规则/字节/工作量预算；buffer API仅适配兼容 |
| [resolveImport/fetchProvider](../../../src/application/desktop_providers.cpp) | 主源先落盘后释放，串行TLS网络/弱票据/代次，输出临时文件收敛为一份完整预检输入 |
| [ConfigTransaction](../../../src/application/desktop_runtime.h)、[import/validate/apply/launch](../../../src/application/desktop_runtime.cpp) | worker私有可信语义的校验/提交/启动转移；无UI验证旁路、停止作废、失败补偿排队 |
| [单测源](../../../tests/unit/provider_stream_test.cpp)、[集成测试源](../../../tests/integration/runtime_test.cpp)、[登记](../../../CMakeLists.txt) | provider/摘要边界、结构与顺序、失败留旧、同内容/变化/取消和阶段释放测试；运行NotRun |

生产数据流：下载有界主源 → 安全持久候选并释放主源字节 → provider逐项64KiB写私有临时文件 → 主源/规则事件流与文本逐行展开至≤16MiB临时输出 → 释放provider文件，最终快照读入一次 → 持久快照与完整预检 → 释放快照字节 → 私有上下文保留语义 → 隔离官方校验会话写盘后释放物化字节 → 排队提交catalog/应用 → 分块复核原文和快照、写不可变artifact → 排队启动并分块复核artifact → 移动语义、物化会话写盘后释放 → Geo/清单核对后释放语义 → 启动owned核心。未应用导入/停机选择/失败/停止均结束私有语义持有。

尚存的受限分配包括下载中的一份主源、最终预检输入、预检规则vector与序列化、节点/组图、解析器token/字段、语义以及校验或启动的短时会话产物；阶段内仍有重叠，不能说完全串行单缓冲/零拷贝。重扫和摘要带来有界磁盘IO成本；原文/展开输入16MiB、provider4MiB/累计16MiB/32项、字段累计4MiB、节点5000/组500/成员100000、规则20000、输出16MiB、整次重扫128MiB/200000事件应在后续真实大公开夹具验证兼容性及峰值。源探测/预检/会话物化仍使用既有有界整份解析，不在本轮继续重构。

**未验收/接续**：新源码编译/链接与所有C++测试仍未运行；定向验证须先确认受限ACL与linked ancestor、TLS/redirect/超时、下载中及排队边界取消/迟到、OS拒删除、坏候选/篡改留旧、同内容PID不变、变化按原事务补偿及P1逐流发布。同步解析期间不会处理中途排队的stop，取消在worker下一安全边界生效；单次工作量有预算，但UI响应与实际延迟未测。正常RAII不保证强杀清理，未实现重启时未知暂存文件回收。客户端组专用工作集/工作集/私有提交、内核单列需针对新构建另测，本轮没有具体MB收益、100MB达标或长期泄漏结论。

优化P3未实施：订阅卡片模型/详情及隐藏卸载策略仍由父任务另行授权；不得为接续把生产发布改回全量snapshot，也不能将本轮私有上下文交给UI或扩大秘密访问。现有显式诊断snapshot的阶段计数只供测试，不能作为持续生产遥测。

## 22. P3-A：稳定订阅模型与虚拟卡片（实现Review，运行NotRun）

- 本轮独立授权仅优化P3及必要测试/文档，不是仓库阶段P3。保留45229be上原47 tracked+67 untracked共114路径，保存[p3基线](client_memory_evidence/p3_baseline.json)的路径/摘要；不构建、不执行CMake/产品生成、不运行旧产品、不访问用户实例/订阅/代理/TUN、不提交。
- [订阅模型](../../../src/presentation/profile_model.h)按稳定profile id增删/移动，维护ID索引；相同元数据无通知，变化只发目标行/roles，拒绝重复/空ID而不部分改表。分组模型在一轮完整更新后同步，按slot仅通知对应行；内部JSON隐式共享，不建立完整QVariant树。JSON通道仍是源变化时的完整快照，GUI仍有线性核对和必要结构移动成本，不宣称worker逐字段补丁。
- [GUI入口](../../../src/presentation/ui_state.cpp)接入模型，兼容profiles getter仅显式读取转换；[首页](../../../qml/features/home/home_page.qml)单ID查询/总数，生产不读整份兼容属性。高频控制/遥测不转换完整订阅表，不回退每秒全量snapshot。
- [订阅页](../../../qml/features/subscriptions/subscriptions_page.qml)用ListView/reuseItems/cacheBuffer=0，每行最多3张[单卡片](../../../qml/components/subscription_card.qml)，580/900断点1/2/3列，行高取可见卡片实际内容高度，不固定小cell；导入表单为滚动头部，长名称/翻译/用量换行，键盘上下移动虚拟行、Tab/Space访问按钮。提取文件小写下划线，通过现有CMake命名循环显式QT_QML_SOURCE_TYPENAME登记；新翻译context含中文词条。
- 所有卡片/按钮按当前profile id触发，不捕获可变行号；背景选择低于按钮。管理/删除/回滚对话框归页面持有，滚出视口/回收卡片不销毁对话框；按单ID变化更新管理资料，删除即安全关闭。名称输入保留当前正在编辑且与旧名不同的草稿；来源密码输入不由元数据刷新覆盖。
- 新增[模型测试源](../../../tests/unit/profile_model_test.cpp)覆盖持久索引/精确roles/slot、不reset、1000项分组/同值、非法身份；[GUI测试源](../../../tests/integration/subscription_ui_probe.cpp)登记英中两目标，覆盖1000项delegate有界/滚动复用、稳定ID按钮/键盘、对话框脱离delegate存活/更新/删除、窄窗及长文本。均未配置/编译/运行，全部NotRun；源码与工程登记不是通过结果。
- 已先同步设计07/13、指南和四入口/日志。静态/格式/词表解析与设计合并待后续执行；编辑器包含路径诊断不是编译结果。下一项仅授权内的隐藏/最小化可选卸载，不更改业务worker/内核/订阅事务。

## 23. P3-B：可选隐藏/最小化卸载（实现Review，运行NotRun）

- 仍用[原偏好](../../../src/infra/storage/settings_repository.h)unloadWhenHidden，默认false；JSON字段/旧ui.ini迁移键不变，旧false保持关闭、旧true保持启用且现在涵盖隐藏与最小化，没有替用户全部改为true或新增同义开关。[设置页](../../../qml/features/settings/settings_page.qml)英文/中文明确“隐藏或最小化5秒”及沿用旧值、托盘限制、后台继续；无托盘时未启用者不能开启，已启用者仍可关闭。偏好保存中取消租约，保存失败不进行自动卸载。
- [延迟策略](../../../app/deferred_ui_unload.cpp)拥有唯一single-shot Timer，绑定票据/引擎代次；重复同代后台事件不累积Timer/无限延后，取消先停表/断连接/失效票据。显示、禁用、保存中、退出、卸载或非后台条件取消，旧事件不能卸载恢复窗口；新增[定时测试源](../../../tests/unit/deferred_ui_unload_test.cpp)仅登记，未编译运行。
- [宿主](../../../app/application_host.cpp)只有当前实际Window Hidden/Minimized且设置就绪/启用、真实托盘对象可见且系统托盘能力可用时安排卸载，超时再次联合核对引擎代次/状态/能力。无托盘最小化保留窗口/任务栏入口；等待期间托盘消失则不卸载，若已隐藏则显示窗口。托盘能力运行变化需真实桌面回归，当前NotRun。
- 卸载保存几何、清页面需求、releaseResources并销毁窗口/QML引擎；UiState及业务SettingsService/worker、内核、订阅事务和计划继续。showing/unloading保护合并重入显示，卸载完成再排一次恢复，已有引擎只复用；页面重新申请本实例需求，不重发业务命令。不清全局组件缓存、不强制GC、不裁剪工作集；引用/引擎销毁不保证OS立即返页。
- [storage测试源](../../../tests/unit/storage_test.cpp)补默认false/旧true/false；[宿主源](../../../app/host_probe.cpp)补无托盘启用后最小化超过5秒保留、重入显示单引擎/需求恢复；原生托盘探针扩展隐藏与最小化延迟、禁用取消、重复隐藏/显示与旧timeout不卸载恢复窗口。所有源未编译运行；旧P1生命周期/100次句柄/旧产品资源证据不用于当前补丁，相关新行为NotRun。
- 已同步设计04/13、指南、任务/清单/入口/日志；一次多文件补丁因中文词条上下文不匹配未应用，核对后重试成功。下一项只做允许的静态、格式、文本解析与文档生成/检查，不运行产品或C++测试。
- 首轮真实文本检查：15个C++格式化/dry-run及4个QML解析均退出0，见[p3静态日志](client_memory_evidence/p3_static.log)，工具PowerShell7.6.6/clang-format21.1.2/qmlformat6.9.0；不代表Qt6.8类型/运行检查。后续人工复核补三对话框显式Overlay锚定，避免大内容坐标偏移；关闭/卸载共用真实托盘能力，析构先置退出态，防销毁回调重建。GUI测试源补真实复用身份和Update按钮不误触Select；均未运行，修订后最终静态检查待执行。
- 窄窗空第二/第三slot改Loader不创建卡片内容，避免单列额外持有两张隐藏卡片；现有localization测试源补新context断言，仍NotRun。最终格式文件数变为16；先同步设计07和台账/清单/日志后，再执行最终允许检查。

## 24. P3工程检查过程（仅静态/文本，不替代运行）

- 第一轮最终候选：16个C++格式/dry-run、两份订阅QML格式和四份受影响QML文本解析退出0，见[p3最终候选文本日志](client_memory_evidence/p3_final_static.log)。设计合并生成及-Check、源码命名、git diff --check退出0；进度退出1，唯一问题是历史报告引用的旧包缺失，未创建假包或改历史证据。详见[设计](client_memory_evidence/p3_design.log)、[命名](client_memory_evidence/p3_source_layout.log)、[进度](client_memory_evidence/p3_progress.log)、[差异](client_memory_evidence/p3_diff_check.log)。日志中的LF/CRLF提示不是空白错误。
- [只读文本辅助](client_memory_evidence/p3_static_review.py)及[结果](client_memory_evidence/p3_text_review.log)退出0：16份C++括号/摘要、工程登记、生产无ui.profiles、订阅不正常reset/无全量Repeater、三个Overlay及空slot Loader文本约束；TS XML良构，63个卡片/设置源文案均有有效译文且占位符匹配。这不是C++语法/类型检查、Qt6.8兼容或运行证据。
- 当时快照56 tracked+84 untracked=140路径（输出日志本身创建存在先后）；原114路径全在且仍有改动，99份字节完全相同、15份为授权续改，删除0，HEAD45229be，未提交。交付前另存最终工作区快照，不将旧114份全部称逐字节未改。
- 检查后源码复核补：延迟策略使用PreciseTimer避免粗定时提前，繁忙可晚于5秒；GUI源兼容QJSValue/直接Map并等待弹层退出动画再断言关闭；模型源补删除后持久索引失效断言。设计04/台账/清单/日志先同步，随后重跑受影响检查；所有新测试继续NotRun。

### 收尾修订后真实复检

| 项目 | 真实结果与证据 | 边界 |
| --- | --- | --- |
| C++格式 | 16份格式化与dry-run退出0，[日志](client_memory_evidence/p3_verified_static.log) | clang-format21.1.2；不是编译/语法或类型验收 |
| QML文本解析 | 卡片/订阅/首页/设置4份退出0，同上日志 | qmlformat6.9.0；不能称Qt6.8类型解析、缓存生成或运行通过 |
| 独立文本复核 | 退出0，[日志](client_memory_evidence/p3_verified_text_review.log) | 16份括号/摘要、登记/生产路径文本约束；TS XML良构，63条卡片/设置源文案有效译文与占位符匹配；未运行lupdate/lrelease |
| 设计合并 | 19分册生成及-Check退出0，[日志](client_memory_evidence/p3_verified_design.log) | 仅文档合并、锚点与链接 |
| 源码命名 | 36目录/165文件、其中128产品源码文件，退出0，[日志](client_memory_evidence/p3_verified_source_layout.log) | 不替代构建或功能 |
| 差异空白 | git diff --check退出0，[日志](client_memory_evidence/p3_verified_diff_check.log) | 保留原工作区；未跟踪源码另做格式/文本复核 |
| 进度 | 退出1，[日志](client_memory_evidence/p3_verified_progress.log) | 唯一问题：20261025_proxy_memory_fixes历史报告引用的dist/proxy_memory_fixes/bin/quickclash.exe本地缺失；未伪造产物 |

环境：Windows11、PowerShell7.6.6、clang-format21.1.2、qmlformat6.9.0。外层命令退出0不是所有子检查通过，进度失败按1保留；后段日志显式UTF-8，首轮终端解码异常不影响后段记录。编辑器缺Qt/moc包含路径诊断不是编译结果，本轮没有配置工具链或构建来消除诊断。

交付文档复核首试：设计/命名/diff退出0，进度仍唯一历史缺包退出1；临时Python命令因cmd/PowerShell嵌套引号被剥离而SyntaxError、退出1，源码摘要比较尚未执行，外层提示“Source digests changed”不是检测到源码变化。失败原文保留在[交付复核](client_memory_evidence/p3_delivery_checks.log)，改用PowerShell原生摘要比较重试，结果以同日志后续重试段为准；不是产品编译/测试失败。

交付重试已退出0：16份源码SHA-256与格式/文本复检版本完全一致，独立文本/114路径保留复核退出0；设计/命名/diff退出0，进度仍仅历史缺包退出1。最终快照确认56 tracked+93 untracked=149路径、原114全部保留（99字节相同、15授权续改）、删除0、HEAD45229be、未提交；没有任何产品/C++/GUI/托盘/内存运行验收。交付记录同步后仅再核对文档及工作区，不改源码。

## 25. P3最终接口与数据/生命周期交接

下列行号指本轮格式化后源码，供父任务定位；具体行为仍需新代码运行验证。

| 定位 | 当前接口与责任 |
| --- | --- |
| [订阅模型声明](../../../src/presentation/profile_model.h)第11—46行；[实现](../../../src/presentation/profile_model.cpp)第13、51行 | 单ID查询返回一条Map；完整订阅JSON按稳定ID增删/移动及精确roles更新，正常不reset；非法/重复ID整批拒绝。源持久索引保留由Qt结构通知维护，删除即失效。 |
| [分组投影](../../../src/presentation/profile_model.cpp)第132—183行 | 每行最多3个JSON共享slot，列数1—3；完整publication后同步，实际变化slot才通知，按需转换被请求的一条Map；不是巨型delegate或完整QVariantList缓存。 |
| [GUI接入](../../../src/presentation/ui_state.cpp)第70—100行；[属性](../../../src/presentation/ui_state.h)第68—76、256—257行 | profileModel/profileRows由UiState持有、跨QML引擎存活；兼容profiles仅显式调用时展开，不常驻第二份完整QVariant树。先验证模型再写appliedData基线；生产QML已无ui.profiles消费者。 |
| [订阅页面](../../../qml/features/subscriptions/subscriptions_page.qml)第16—62、194—329行；[单卡](../../../qml/components/subscription_card.qml)第12—23行 | 580/900断点，ListView可视范围复用，cacheBuffer为0；行高按真实内容最大高度，无固定小cell，空slot Loader不创建卡片。导入完整保留，Tab/Space与上下方向键、长文本换行有测试源但未运行。 |
| 同页面第33—57、75—193行 | 所有选择/更新/管理及后续命令使用当前稳定ID；三种Overlay对话框由页面持有，不依赖卡片存活。按单ID刷新资料，条目删除关闭管理/回滚/删除弹层；更名草稿与秘密来源输入不被无关刷新覆盖。 |
| [首页](../../../qml/features/home/home_page.qml)第12—22行；[设置](../../../qml/features/settings/settings_page.qml)第395—410行 | 首页只查询当前ID/模型count；低内存文案英文源及中文词表说明默认关闭、沿用旧偏好、隐藏/最小化5秒、托盘限制及业务继续。JSON/旧ui.ini键及默认false未变，旧true明确扩展覆盖最小化。 |
| [延迟租约](../../../app/deferred_ui_unload.h)第11—24行；[实现](../../../app/deferred_ui_unload.cpp)第4—31行 | 默认5000ms唯一single-shot精确定时器；同代引擎重复后台事件不新增计时/无限延后；取消停表/断连接/使票据失效，due携代引擎代次供宿主复核。 |
| [宿主](../../../app/application_host.cpp)第27—65、148—218、230—292行 | 实际隐藏/最小化、偏好就绪且启用、真实可见托盘联合决定；无托盘保留任务栏窗口，超时能力消失不卸载，已隐藏则显示。显示/禁用/保存中/退出取消；showing/unloading和一次排队恢复合并重入，旧回调不卸载已恢复/新代引擎。卸载仅销毁界面、释放图形及页面需求，业务/内核/订阅事务继续。 |
| [工程登记](../../../CMakeLists.txt)第128—182、251—264、285—325行；[探针入口](../../../app/main.cpp) | 新模型/单卡/租约加入生产源码；profile_model、deferred_ui_unload及ui_subscriptions英中两个目标登记；新订阅探针为隔离合成输入，不绑定SettingsService或执行真实业务。只编辑工程文件，未执行CMake或产品生成。 |

测试源：[模型](../../../tests/unit/profile_model_test.cpp)、[延迟](../../../tests/unit/deferred_ui_unload_test.cpp)、[英中GUI](../../../tests/integration/subscription_ui_probe.cpp)、[storage](../../../tests/unit/storage_test.cpp)、[localization](../../../tests/unit/localization_test.cpp)、[宿主/原生托盘](../../../app/host_probe.cpp)。覆盖意图见第22—24节；源码断言存在不等于覆盖已生效。原生托盘入口仍需真实桌面，不能以offscreen探针代替。

## 26. P3最终工作区、NotRun与后续审查

- [本轮初始基线](client_memory_evidence/p3_baseline.json)为HEAD45229be4960d00d86d10f2b6c280105f99b3ac7f、47 tracked+67 untracked共114路径。当前56 tracked+93 untracked共149路径；原114路径均存在且仍保有工作区改动，99份SHA-256相同、15份仅授权续改，删除0，无提交。收尾文档检查与最终状态保存至[交付复核](client_memory_evidence/p3_delivery_checks.log)及[工作区快照](client_memory_evidence/p3_final_worktree.log)。不将全部原文件称逐字节未变，完整JSON基线保留原始摘要。
- **NotRun**：CMake配置/生成/编译、Qt产品代码生成/打包；所有新/受影响C++单测、GUI英中与1000项真实delegate上界/复用、键鼠/窄窗长文本、管理弹层、默认/旧值持久化、无托盘/原生托盘/显示禁用退出与旧回调竞态、需求去重、后台事务连续性；真实用户订阅更新与内存、长时/压力/泄漏验收均未执行。不运行旧产品、不操作用户实例/真实订阅/系统代理/TUN；没有本轮MB节省、100MB达标或长期泄漏已解决结论。
- 残余风险：Qt6.8上QML类型/Bound Loader、实际可变行高/滚动估算/焦点回收、模板测试源码本身及原生时序尚需编译运行；托盘在卸载前有联合能力复核，已卸载后桌面/托盘丢失的恢复仍需真实OS回归。模型在元数据发布/列数变更有线性核对，任意结构重排/索引重建可能更高成本；worker仍按源变化发布完整JSON，不是端到端字段补丁。UiState保留轻量订阅元数据，QML卸载不等于所有业务缓存消失或OS立即返页。
- 逐项设计04/07/13、用户指南、TASKS/CHECKLISTS/DEVLOG/报告索引及进度入口已同步。MVP-05/06保持Stale，G2/G3保持NotRun，无新增产品Pass，不改变仓库阶段任务数量；优化P3不是仓库阶段P3完成。
- 下一步仅由父任务取得新构建/运行授权后，先做新源码Windows增量构建及模型/定时/英中订阅GUI/storage/localization/宿主定向回归，再独立进行真实托盘和必要单次资源短测；本轮到不构建交付为止，不自动启动后续优化或性能矩阵。

## 27. P0—P3最终跨批次静态集成审查（2026-10-26，Zoo）

本节起为最终跨批次审查，不是重做各批实现，也不是仓库阶段P0—P3完成。结论：**P0、P1-A、P1-B、P2、P3均实现Review，全部产品验证NotRun**。本轮未确认可静态定性的产品接缝缺陷，未改产品源码、测试源、设计或指南；不为获得“修复数”改变行为。新增内容仅为审查辅助脚本/证据和相关六份进度文档。审查未发现明确缺陷，不等于证明不存在缺陷。

考虑七类来源：工程漏登记；直接依赖/声明定义/模板及信号槽不匹配；会话与需求代次乱序；关闭屏障/回调所有权；provider/Geo安全存储与解析预算；订阅可信上下文/忙碌交接与取消；虚拟列表/管理稳定ID和引擎恢复。优先沿“工程依赖”和“排队事务/生命周期”两条链核对；无已确认产品诊断，因此没有进入产品修复或添加运行日志的阶段。以下是源码静态观察，不冒充编译或运行断言：

- 工程：核对[主工程目标及headless分支](../../../CMakeLists.txt#L30)、[运行时/服务依赖](../../../CMakeLists.txt#L65)、[测试目标](../../../CMakeLists.txt#L197)。新增实现、带元对象的头文件、QML导出及英中GUI探针均有登记；两个纯头文件通过直接包含使用。headless保留Core/storage/config测试，UI分支持有Network/runtime/Qt Test；没有执行任何目标组合的配置或构建。文本包含路径存在检查不能证明头文件自包含、重载选择、访问权限、模板、MOC或链接正确。
- 通道：[需求与请求票据](../../../src/application/runtime_update.h#L12)、[邮箱投递/关闭同锁](../../../src/infra/storage/latest_mailbox.cpp#L21)、[存储worker屏障](../../../src/infra/storage/settings_service.cpp#L75)、[GUI需求与模型](../../../src/presentation/ui_state.cpp)静态连通。完整通道可覆盖，控制/Operation不进入邮箱；请求联合会话/需求/操作/请求代次，撤销后清基线，再申领可完整重发。邮箱解锁后通知消费者，未见此链引入新的强引用回调环。
- 日志：[worker按需释放及有界环](../../../src/application/desktop_features.cpp#L158)、[服务日志确认状态](../../../src/infra/core/engine_session.h#L15)、[服务关闭处理](../../../src/platform/windows/service_host.cpp#L362)保留。页面暂停保留有界冻结/最新待恢复值，离页释放；App 100条、页面1000条、服务200条和消息限额仍在，不宣称原通用2000条/4MiB设计已验收。
- 存储/解析：[统一有界读取](../../../src/infra/io/bounded_read.h#L19)、[仓库路径/摘要/提交](../../../src/infra/storage/json_store.cpp)、[Geo专用写入](../../../src/infra/storage/geo_resource_writer.cpp#L39)、[provider暂存](../../../src/infra/storage/provider_stage.cpp)、[流式展开](../../../src/infra/config/config_provider_stream.cpp#L102)核对限额、长度/摘要、ACL入口、不可变原子提交、多文档/别名/标签/深度/schema拒绝及累计工作量预算。未删除失败留旧保护；SHA-256不等于上游签名真实性。
- 事务：[下载正文移交](../../../src/application/desktop_runtime.cpp#L891)、[provider弱回调和串行交接](../../../src/application/desktop_providers.cpp#L53)、[私有校验上下文](../../../src/application/desktop_runtime.cpp#L1062)、[可信立即应用](../../../src/application/desktop_runtime.cpp#L1281)、[启动移动语义](../../../src/application/desktop_runtime.cpp#L1511)、[停止失效](../../../src/application/desktop_runtime.cpp#L1872)串联核对。成功排队边界保持忙碌，完成回调核对身份/代次；偏好、原文/快照及artifact摘要仍匹配，停止/失败清上下文，候选按catalog接管边界清理。旧会话补偿路径保留，未用跳过校验/清理换取降内存。
- 界面：[稳定ID模型](../../../src/presentation/profile_model.cpp#L51)、[分组行投影](../../../src/presentation/profile_model.cpp#L156)、[虚拟订阅页](../../../qml/features/subscriptions/subscriptions_page.qml)、[独立卡片事件](../../../qml/components/subscription_card.qml)、[卸载租约](../../../app/deferred_ui_unload.cpp)、[宿主恢复](../../../app/application_host.cpp)核对角色、空slot、对象复用后的当前ID、页面持有管理弹层、单引擎重建和无托盘任务栏退路。全部生产QML没有旧完整订阅属性消费者；显式诊断/既有探针使用兼容快照，不隐式申领页面需求。

## 28. 最终实现/验证矩阵

| 优化切片 | 实现状态 | 已有定向测试源（仅登记/静态审阅） | 本轮真实验证 | 产品结论 |
|---|---|---|---|---|
| P0 | Review | [有界读取](../../../tests/unit/bounded_read_test.cpp)、[Geo写入](../../../tests/unit/geo_resource_writer_test.cpp)、[存储](../../../tests/unit/storage_test.cpp) | 独立PowerShell自身采样3项退出0；受影响格式/文本检查 | C++/Geo/ACL/GUI/产品内存NotRun |
| P1-A | Review | [邮箱](../../../tests/unit/latest_mailbox_test.cpp)、[发布](../../../tests/unit/runtime_publication_test.cpp)、[列表模型](../../../tests/unit/runtime_list_model_test.cpp) | 全部受影响格式、QML文本及工程文本核对 | 线程/可靠事件/模型/产品NotRun |
| P1-B | Review | [GUI状态](../../../tests/unit/ui_state_test.cpp)、[页面探针](../../../tests/integration/runtime_ui_probe.cpp)、[读取与日志票据](../../../tests/unit/runtime_publication_test.cpp) | 代次、清理、日志有界性静态链核对 | 并发/服务IPC/隐藏恢复/产品NotRun |
| P2 | Review | [provider暂存/展开](../../../tests/unit/provider_stream_test.cpp)、[更新/取消行为源](../../../tests/integration/runtime_test.cpp#L917) | 安全入口、预算、弱上下文/排队交接静态核对 | YAML行为/官方校验/事务/取消/真实网络NotRun |
| P3 | Review | [订阅模型](../../../tests/unit/profile_model_test.cpp)、[卸载定时](../../../tests/unit/deferred_ui_unload_test.cpp)、[英中订阅GUI](../../../tests/integration/subscription_ui_probe.cpp)、[宿主](../../../app/host_probe.cpp)、[词表](../../../tests/unit/localization_test.cpp) | QML文本、翻译占位符、稳定ID/引擎所有权静态核对 | GUI/复用上界/键鼠/真实托盘/恢复/内存NotRun |

未新增或执行产品测试；不运行旧产品来证明新代码。本轮没有具体MB节省、客户端100MB达标或泄漏解决结论；MVP-05/06保持Stale，MVP Pass=0/16，G2/G3保持NotRun，仓库任务计数不变。

## 29. 本轮实际命令、退出码与版本

可复核入口为[最终检查脚本](client_memory_evidence/final_review_static.py#L1)，全部参数/58份C++和9份QML的具体名单见[最终日志](client_memory_evidence/final_review_static.log)。脚本总退出0仅表示它验证了预期检查结果，**不把子项进度退出1变成通过**。

| 实际命令/范围 | 真实退出码 | 证据与边界 |
|---|---|---|
| [pwsh -NoProfile -File tests/scripts/attach_memory_test.ps1](../../../tests/scripts/attach_memory_test.ps1#L1) | 0 | [独立采样日志](client_memory_evidence/final_review_attach_memory.log)；仅测试PowerShell自身，三指标、重复PID拒绝、缺失PID拒绝共3项；不测产品 |
| clang-format --dry-run --Werror，58个受影响C++文件 | 0 | [格式日志](client_memory_evidence/final_review_format.log)；只是格式，不是C++语法/类型或编译 |
| qmlformat，9个受影响QML文件逐一文本解析，无原地写入 | 各0 | [具体命令及文件](client_memory_evidence/final_review_static.log)；没有执行QML类型注册或GUI |
| [标准PowerShell Parser，3个受影响脚本](client_memory_evidence/final_review_static.py#L153) | 0 | [语法日志](client_memory_evidence/final_review_script_parse.log)；不运行脚本内产品操作 |
| JSON/XML、189处受影响QML翻译调用/占位符、局部包含路径、括号及工程文本登记 | 0（汇总脚本） | [最终日志](client_memory_evidence/final_review_static.log)；不是C++语法/模板/信号槽验证，不等于翻译GUI已运行 |
| [pwsh -NoProfile -File scripts/build_design_docs.ps1 -Check](../../../scripts/build_design_docs.ps1#L1) | 0 | [设计一致性日志](client_memory_evidence/final_review_design_check.log)；本轮未改设计，无需重新生成，不调用产品资源生成器 |
| [pwsh -NoProfile -File scripts/check_source_layout.ps1](../../../scripts/check_source_layout.ps1#L1) | 0 | [布局日志](client_memory_evidence/final_review_layout.log) |
| [pwsh -NoProfile -File scripts/check_progress.ps1](../../../scripts/check_progress.ps1#L1) | **1** | [原始进度日志](client_memory_evidence/final_review_progress.log)；唯一既有历史包链接缺失；不伪造产物、不构建包、不删除历史事实 |
| git diff --check | 0 | [差异检查日志](client_memory_evidence/final_review_diff.log) |
| [python -X utf8 docs/progress/reports/client_memory_evidence/final_review_static.py](client_memory_evidence/final_review_static.py#L1) | 首轮1，修订/重跑0 | [首轮失败](client_memory_evidence/final_review_static_first.log)与[最终日志](client_memory_evidence/final_review_static.log)；首轮为辅助日志解码失败，各子项退出码已真实打印 |

[版本日志](client_memory_evidence/final_review_versions.log)：PowerShell7.6.6、clang-format21.1.2、qmlformat/qmllint6.9.0、Python3.14.4。仅运行qmllint版本查询，没有执行依赖生成类型的类型检查。Qt文本工具6.9.0不证明最低声明Qt6.8兼容。

## 30. 证据过程纠错及最终工作区

- 初始[状态记录](client_memory_evidence/final_review_baseline_status.txt)共149路径；首次基线命令受中文路径/终端解码影响退出1，重试退出0却只生成[140项SHA清单](client_memory_evidence/final_review_baseline_manifest.json)。本轮主动核对发现遗漏9份中文设计路径，不能称149份都有初始SHA。140项在写回进度前全部字节未变；最终134项字节不变、6份进度文档为本轮授权调整。9份设计路径仍保留且未编辑，另存[UTF-8补充摘要](client_memory_evidence/final_review_utf8_supplement.json)，明确其不是初始摘要证据；不覆盖原错误记录。
- 辅助缺项输出首试因终端GBK编码退出1，显式UTF-8重试0。汇总首轮读取PowerShell异常输出时解码失败退出1，保留原日志后改用ASCII字节识别错误条目并重跑0。静态检查成功后附带的临时快照命令因嵌套引号SyntaxError退出1、未写快照；直接调用重试0。这些是审查工具/证据处理失败，不是产品失败，也不记为产品通过。
- [最终工作区快照](client_memory_evidence/final_review_workspace.json)：**56 tracked+119 untracked=175路径**，初始149路径保留，删除0，HEAD仍45229be4960d00d86d10f2b6c280105f99b3ac7f，未提交。本轮新增26份辅助脚本/证据，调整6份既有进度文档；产品源码、测试源、设计和指南没有本轮字节修改。原有149路径的状态与本轮新增证据严格分开，不把175称作全部新实现。
- 六份受影响文档为[进度入口](../README.md)、[任务](../TASKS.md)、[清单](../CHECKLISTS.md)、[开发日志](../DEVLOG.md)、[报告索引](README.md)及本持续报告。按用户要求，入口/任务/清单/索引移除累计开发流水，只保留当前状态、规则、验收项和证据导航；过程集中于开发日志，详细验证集中于报告。61条任务记录和59条阶段门/MVP/勾选记录逐条不变，文档专用复检设计/布局/diff退出0，进度退出1且仍只有原历史缺包；不重跑产品。

## 31. 未关闭的验证风险与下一步

1. **构建/类型风险未关闭**：主程序/服务/headless/testing各组合均未配置编译，直接include的存在性不证明自包含；声明/重载/访问、Qt6.8模板测试API、MOC元类型、QML导出及Bound/Loader类型和链接须由新源码构建确认。没有CMake配置/生成、预处理/编译/链接/打包，也没有moc/qmltypes/qmlcachegen/lrelease。
2. **时序与安全运行风险未关闭**：worker屏障/邮箱关闭与迟到回调、日志开关服务确认、需求重进完整重发、校验/提交/启动/补偿取消需真实定向回归；暂存ACL、junction竞态、短读和OS拒删/强杀残留未实测。未请求真实订阅/Geo，不操作QuickClash/Mihomo、服务、代理、TUN或用户实例，不能据此关闭服务/网络/ACL验收。
3. **GUI/资源风险未关闭**：可变行高滚动估算、真实delegate/焦点回收、复用后按钮ID、管理弹层、旧偏好持久化、显示/禁用/退出旧租约、无托盘最小化和原生托盘恢复均未运行；已卸载后托盘丢失仍需真实OS恢复验证。完整JSON发布/轻量业务元数据及分配器常驻仍存在；释放引用不等于OS返还物理内存。
4. 本轮没有另一个“已确认却绕过保护”的产品缺陷；以上是未关闭验证风险而非已复现根因。后续仅在另获构建授权后，以新源码Windows增量构建和表中受影响定向测试补证，再按需做一次只读资源短测；长期/压力/帧/GPU专项仍按原计划后置。静态总汇不是产品验收通行证。

## 32. 代理/日志页切换内存优化（2026-10-26，Zoo）

- 关联P3-01第六轮；用户反馈切换到代理和日志页内存上升明显。根因：QML delegate 绑定每次求值都直接访问 `ui.runtime/ui.nodes/ui.groups/ui.delays/ui.telemetry`，每次访问把整通道 QVariantMap/List 重新物化为 JS 对象树；代理页每个节点卡 4 处绑定 × 每秒通知，日志页每秒 traffic 样本触发 dropped 计数与导出按钮绑定重复展开整表；worker `appendLog` 每条日志构造两个临时 `QRegularExpression` 重复编译。
- 修复：[代理页](../../../qml/features/proxies/proxies_page.qml)第12—34行通道值改页面级只读属性（每次通知只物化一次），`nodeByName/groupNames` O(1) 索引替代 `nodes.find/groups.some` 线性扫描；[日志页](../../../qml/features/logs/logs_page.qml)第17—18行 `telemetry/runtime` 同样缓存；[worker](../../../src/application/desktop_features.cpp)第157—170行正则改 `static const` 单次编译。未改通道协议、需求代次或模型行为。
- 真实验证：`cmake --build build/dev --target quickclash` 退出0（QML 缓存重新生成编译）；`ctest -R qml_smoke|ui_responsive|ui_runtime_lists` 3/3 通过；clang-format dry-run 与 qmlformat 格式一致。内存收益未实测：需附着真实实例用 [attach_memory_windows.ps1](../../../bench/attach_memory_windows.ps1) 做切换页面前后 10—30 秒专用工作集短测，本轮 NotRun，不宣称具体 MB 节省或 100MB 达标。
- 未提交；保留全部既有工作区改动。
