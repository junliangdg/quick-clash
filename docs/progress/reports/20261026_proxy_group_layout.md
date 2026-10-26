# 首页精简与代理组布局调整

## 1. 范围与版本

- 日期：2026-10-26；执行者：Zoo；关联任务：P3-01。
- 工作区补丁，未提交；首轮按用户约定不运行 CMake、产品构建、CTest、GUI、内核或资源测量；2026-10-26 第二轮补真实增量构建与探针结果（见第 4 节）。
- 保留现有回放数据能力及设置页诊断入口，只移除首页状态卡的回放入口和专用回放显示。

## 2. 实现

### 首页

- 订阅概览卡只保留当前订阅、订阅/代理组/节点数量、订阅详情和管理入口。
- `runtimeStatus`、`runtimeError`、核心版本、本地代理状态与运行时间集中到“连接与系统”。
- 上传和下载合并为单个网络流量卡，保留各自速率、历史和 1 Hz 实时数据来源。
- 移除首页 `Preview replay`/`End replay`、演示订阅及合成流量文案；首页网络工具卡不再重复核心 Ready/Stopped。
- 界面中的内核显示名统一为 `Clash Meta (mihomo)`。

### 2026-10-26 第四轮：默认窗口尺寸与启动居中

- `qml/main.qml` 默认窗口尺寸由 1160×780 改为 1000×640 逻辑像素；宽度 1000 为左侧图标＋名称侧栏完整显示的最小断点（`compact: width < 1000`，此时侧栏 192），960 宽度下侧栏仅显示 64 图标轨。
- `app/application_host.cpp` 新增 `centerInitialWindow()`：进程内首次显示（无已保存几何）时，窗口在鼠标所在屏幕的可用区域居中，无屏幕信息时回退窗口所在屏/主屏；隐藏或卸载前保存的几何在恢复显示时仍优先于默认尺寸与居中。`--window-size` 请求尺寸在引擎创建后立即应用，不再延后到 `showNormal()` 之后。
- 用户实测反馈 125% DPI 下 1200×800 逻辑像素被放大为 1500×1000 物理像素，先按 DPR 换算后按用户决定改固定逻辑尺寸，再按用户要求将宽度对齐侧栏完整显示的最小断点 1000；设计 04 第 3 节与 17 第 3 节同步该稳定方案；探针均自行 resize，不依赖默认位置。

- 首页订阅概览卡移除 `Manage subscriptions` 按钮；订阅管理仅经侧栏/底栏导航进入，卡片保留订阅详情。
- 流量卡移除底部重复的 `Waiting for core data` 提示；等待状态仅由标题行右侧状态文本表达。
- 代理页测速进度行的 `Cancel` 按钮 `implicitHeight` 绑定同行进度文本 `implicitHeight`，按钮显示/隐藏不再改变行高。

### 2026-10-26 第八轮：流量卡对齐、节点卡紧凑、连接页操作行与底部导航

- 首页流量卡与订阅概览卡同处一行时高度一致（两卡 `Layout.fillHeight`，网格行高取较高者）；卡内上传/下载颜色标签改为上下单列排列，两个速率数值并排显示在标签列右侧，卡片最小宽度随之缩小。
- 代理页节点卡改紧凑单行样式：名称单行省略（`maximumLineCount: 1`）、内边距 10→8、行距 4→2、测速按钮 34×26→30×24，卡片高度不随文本行数增长。
- 连接页两个关闭按钮（全部/筛选）合并同一行；“连接”标题与排序下拉框同处一行，下拉框采用与按钮一致的圆角、inset 底色与 hover 高亮风格（`accentSurface`），`RuntimeList` 支持空标题隐藏内置标题行。
- 底部导航改为纯图标入口（高 68→56，不显示文字，标题经工具提示与可访问名称提供）；紧凑导航投影在代理与设置之间新增订阅入口（`compactNavigation` 由 home/proxies/settings 改为 home/proxies/subscriptions/settings），`ui_state_test` 断言同步为四项。
- 设计 08（流量卡标签布局、节点卡紧凑单行）、09（连接页操作行）、17（底栏四项图标化）已同步稳定方案。


- 代理页测速进度行改为展开期间常驻显示：未测速时显示“未测速”占位文本，开始/完成/取消测速只切换文本内容，不再显示/隐藏整行，组卡高度在测速状态变化前后保持不变，消除点击测速后的高度跳变。
- 代理组展开由“同一时间只展开一个组”改为各组独立展开/收起：`expandedGroup` 单字符串改为 `expandedGroups` 列表，点击组头切换该组自身展开状态，互不影响其他组；展开集合按订阅持久化（`saveState` 的 `groups` 键，有界去重列表，最多 32 项、单项 128 字符），恢复时优先已保存集合，无记录时回退当前出口策略组。
- `src/presentation/ui_state.cpp` 的 `saveState` 边界过滤新增 `groups` 键：接受字符串列表，逐项限制长度与空字符、去重并截断至 32 项；其余键的过滤行为不变。
- 代理页组列表由 `ListView`（可变高度 delegate 回收导致滚动中 contentHeight 估算跳变、滚动条长度跳变）改为 `Flickable` + `ColumnLayout` 直接布局；组卡数量少，滚动条长度由真实布局决定。节点 `Repeater` 模型改为仅展开组实例化，折叠即释放节点卡。

### 2026-10-26 第二轮：五项界面反馈

- 订阅卡片：管理按钮从底部操作区移到标题行右上角，底部仅保留更新按钮。
- 流量卡：上传/下载合并到同一坐标系（上传橙色、下载蓝色，共用最大值刻度）；波形占整卡一行，速率文本与图例另起一行；`traffic_sparkline.qml` 改为双序列画布并修复数据更新不重绘的问题。
- 代理页：节点网格改为 `Flow` 全量布局，仅保留页面级滚动条；组卡测速改为标题行网络测速图标（`⚡`，objectName `test_group`）；节点卡去除 Select 按钮，点击卡片即选择；协议/UDP 摘要、延迟与测速图标（objectName `test_node`）同行紧凑布局。
- 窗口图标：托盘与顶层窗口共用同一 `buildApplicationIcon()` 绘制的品牌图标。
- 导航：侧栏与底部导航标题加大加粗（15/13 px bold）。

### 代理页与设置

- 代理页主体改为虚拟代理组卡片列表，同一时间展开一个组，并按订阅保存展开位置。
- 组卡显示名称、策略、成员数和当前成员；展开后保留该组批量测速、取消及进度。
- 节点使用紧凑圆角卡片和有界高度多列 `GridView`；列数随宽度变化，内部滚动，未展开组不创建节点网格。
- 节点卡显示协议及配置显式声明的 UDP 能力；动态提供者节点缺少内联协议摘要时不虚构协议，也不因此禁用测速。
- 数值延迟按 `<300 ms` 绿、`300—799 ms` 黄、`>=800 ms` 红；不可用为红，测速中为黄，未知为次要文字色。
- 节点测速 HTTPS 目标移至设置页，与网站测试目标分离；`delay_target` 命令校验、去首尾空白、使用独立凭据引用持久化并发布 `delayTarget`，测速批次运行中禁止替换。
- 删除代理页顶部订阅管理、刷新、出口重复信息、组选择器、页内目标输入、全部节点测速和筛选表单。

## 3. 源码与测试位置

- QML：`qml/features/home/home_page.qml`、`qml/features/proxies/proxies_page.qml`、`qml/features/settings/settings_page.qml`、`qml/features/subscriptions/subscriptions_page.qml`。
- 运行状态与摘要：`src/application/desktop_features.cpp`、`src/application/desktop_runtime.cpp`、`src/infra/config/config_preflight.cpp`、`src/presentation/ui_state.cpp`。
- 多语言：`resources/i18n/quickclash_zh_cn.ts`、`tests/unit/localization_test.cpp`。
- 行为断言：`tests/unit/contracts_test.cpp` 增加协议/UDP 摘要检查；`tests/integration/runtime_test.cpp` 增加测速目标校验、去空白和发布检查。

## 4. 验证记录

| 检查 | 结果 | 边界 |
|---|---|---|
| `qmlformat -i` 与 `clang-format -i` 的组合命令 | 首次退出 1，无诊断输出 | 保留为辅助命令历史；不据此判定源码失败或通过 |
| 分开执行 `qmlformat --check`（4 份受影响 QML） | Pass，退出 0 | 格式/文本检查，不是 Qt 6.8 类型或运行验收 |
| 分开执行 `clang-format --dry-run --Werror`（7 份受影响 C++） | Pass，退出 0 | 格式检查，不是编译 |
| `git diff --check`（首轮实现后） | Pass，退出 0 | 仅 LF→CRLF 提示 |
| 产品构建、QML lint、CTest、GUI 截图与真实测速 | NotRun | 按用户约定不构建，不冒称产品验收通过 |
| 2026-10-26 第二轮：`cmake --build build/dev --target quickclash`（MSVC 14.51/Qt 6.9.0，增量） | Pass，退出 0 | 真实编译链接通过；全量构建因工作区既有 `provider_stream_test.cpp` 的 `QHash::first()` 编译错误失败，该文件属上一轮未完成改动，非本轮引入 |
| 2026-10-26 第二轮：`quickclash.exe --smoke-test --isolated` | Pass，退出 0 | QML 加载与页面就绪 |
| 2026-10-26 第二轮：`quickclash.exe --subscription-ui-test --isolated` | Pass，退出 0 | 订阅卡片管理按钮迁移后键盘/点击/复用断言全部通过 |
| 2026-10-26 第二轮：`quickclash.exe --tray-test --isolated` | Fail（`minimized tray host unloads after delay`） | 5.5 秒延迟卸载时序断言失败；与本轮 UI 改动无关的既有长时探针，标记 Stale/待复测 |
| 2026-10-26 第三轮：`cmake --build build/dev --target quickclash`（MSVC 14.51/Qt 6.9.0，增量） | Pass，退出 0 | 真实编译链接通过；本轮仅改 `home_page.qml`/`proxies_page.qml` |
| 2026-10-26 第三轮：`cmake --build build/dev --target quickclash_qmllint` | Pass，退出 0（本轮两份 QML 无警告） | 输出仅含 `subscriptions_page.qml` 既有 `missing-property` 警告，非本轮引入 |
| 2026-10-26 第三轮：`ctest -R 'qml_smoke|ui_responsive|ui_runtime_lists'` | Pass，退出 0（3/3） | QML 加载、宽窄窗口布局与运行列表探针通过；`ui_keyboard`/`ui_subscriptions` 失败为 DEVLOG 已记录的既有字体目录/raise 平台问题，非本轮引入 |
| 2026-10-26 第四轮：`cmake --build build/dev --target quickclash`（MSVC 14.51/Qt 6.9.0，增量） | Pass，退出 0 | 真实编译链接通过；首次裸终端缺 MSVC INCLUDE 环境变量失败，经 vcvars64 初始化后构建产物时间戳确认包含本轮 `application_host.*`/`main.qml` 修改；两次 LNK1104 因运行中的 quickclash.exe 占用输出文件，结束进程后重试通过 |
| 2026-10-26 第四轮：`ctest -R 'qml_smoke|ui_responsive|ui_runtime_lists'` | Pass，退出 0（3/3） | QML 加载、宽窄窗口布局与运行列表探针通过；默认居中在无真实 GUI 断言的探针外，属人工验收范围 |
| 2026-10-26 第四轮：`pwsh scripts/build_design_docs.ps1`、`-Check`、`check_source_layout.ps1` | Pass，退出 0 | 设计合并重新生成并一致；36 目录/165 文件命名通过 |
| 2026-10-26 第四轮：`pwsh scripts/check_progress.ps1` | Fail，退出 1（仅1项） | 唯一错误为 `20261025_proxy_memory_fixes.md` 引用的旧 dist 包缺失，DEVLOG 已记录的既有历史断链，非本轮引入 |
| 2026-10-26 第五轮：`cmake --build build/dev --target quickclash`（MSVC 14.51/Qt 6.9.0，增量） | Pass，退出 0 | 真实编译链接通过；首次裸终端缺 MSVC INCLUDE 环境变量 LNK1181 失败，vcvars64 初始化后重试通过 |
| 2026-10-26 第五轮：`cmake --build build/dev --target quickclash_qmllint` | Pass，退出 0 | 本轮 `proxies_page.qml` 无警告；输出仅含既有 `subscriptions_page.qml` missing-property 警告，非本轮引入 |
| 2026-10-26 第五轮：`ctest -R 'qml_smoke|ui_responsive|ui_runtime_lists'` | Pass，3/3 | QML 加载、宽窄窗口布局与运行列表探针通过 |
| 2026-10-26 第五轮：`cmake --build build/dev --target localization_test` + `ctest -R localization` | Pass，退出 0 | 新增“未测速”词条目录加载与中文翻译断言通过 |
| 2026-10-26 第五轮：`qmlformat --check`（`proxies_page.qml`）、`clang-format --dry-run --Werror`（`ui_state.cpp`） | Pass | 格式化后与 qmlformat 输出逐字节一致（PowerShell 管道编码显示差异为假阳性）；C++ 无格式违规 |
| 2026-10-26 第五轮：`pwsh scripts/build_design_docs.ps1`、`-Check`、`check_source_layout.ps1` | Pass，退出 0 | 设计合并重新生成并一致；36 目录/165 文件命名通过 |
| 2026-10-26 第五轮：`pwsh scripts/check_progress.ps1` | Fail，退出 1（仅1项） | 仍为 `20261025_proxy_memory_fixes.md` 旧 dist 断链，既有历史问题，非本轮引入 |
| 2026-10-26 第八轮：`cmake --build build/dev --target quickclash`（MSVC 14.51/Qt 6.9.0，增量） | Pass，退出 0 | 真实编译链接通过；首次裸终端缺 MSVC INCLUDE 环境变量失败（C1083 `type_traits`），vcvars64 初始化后重试通过，QML 缓存重新生成并编译 |
| 2026-10-26 第八轮：`ctest -R 'ui_state|qml_smoke|ui_responsive|ui_runtime_lists'` | Pass，退出 0（4/4） | `ui_state` 四项紧凑导航断言、QML 加载、宽窄窗口布局与运行列表探针通过 |

## 5. 状态与未覆盖项

- 第五轮实现已完成并经真实增量构建、qmllint、qml_smoke/ui_responsive/ui_runtime_lists、localization 与格式/设计/布局检查验证；任务保持 Doing/验证中，不改变 G2/G3、MVP 或内存专项结论。
- 多组独立展开/收起与展开集合持久化、测速行常驻的真实 GUI 交互（多组同时展开显示、跨会话恢复）属人工验收范围，探针未断言展开集合行为。
- 全量构建仍因既有 `provider_stream_test.cpp`（`QHash::first()`）编译错误失败，非本轮引入；托盘长时探针 Stale 待复测。
- 未运行资源短测；本次不声明内存变化或 100 MB 达标。
