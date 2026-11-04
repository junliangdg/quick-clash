# P1 平台壳与移动边界

桌面壳使用八个 moduleId 的左侧导航投影，工厂注册与导航位置无关。FeatureRegistry::project 可选取、去重和排序入口；resolve / resolveEmbedded 始终独立校验上下文，隐藏入口不能成为授权机制。

窄窗使用home/proxies/subscriptions/settings四个底部入口，其余页面从设置的“其他”经明确路由访问，并在页头返回设置。600逻辑像素为底栏断点，600—999为64px图标栏，1000—1199为192px侧栏，至少1200为208px侧栏；最小窗口360×480。PageHost与UiState不因导航容器改变而替换，保留当前页与状态。移动安全区、返回栈、触摸及VPN宿主仍未实现。

P2-24由main.qml明确侧栏宽度/底栏高度，以锚点确定页面矩形；导航按钮/图标几何独立于名称显隐，不通过外层嵌套Layout与footer重新协商。PageHost仍为同一实例，页面内Layout可继续自适应，连续resize不触发重新导航、引擎重建或额外需求。现有responsive探针覆盖首页/代理各80步跨1000/600断点，断言加载内容指针、引擎和需求数量保留；耗时是事件处理并含短等待，不是呈现帧时间。

页面通过注入的 UiState 和 ThemeTokens 访问展示状态与路由意图；PageHost 不持有业务服务，完整页不能嵌入首页。EmbeddedHost 使用稳定 contentId / 独立 instanceKey，每个可见实例申请自己的需求，隐藏或销毁释放；参数或实例键变更先释放旧实例，无效上下文清空内容。P1 仅实现代理摘要这一代表内容。未来同模块多业务上下文须扩展状态键及有界投影，不能直接共享完整页对象。

界面的 Light/Dark 颜色、尺寸和状态集中在ThemeTokens；ActionButton、ThemedField/TextArea、ThemedSwitch/CheckBox/ComboBox/SpinBox、ThemedDialog/ToolTip/Progress及StateSurface/FocusRing供全部页面复用。字体由宿主以完整QFont注入，保留Qt字体回退与符号图标。强调色前景属性名为accentForeground，避免onAccent被QML当作信号处理器；主题变化只更新token绑定，不重建页面或引擎、不提交表单草稿。流量由UiState维护固定60点、1Hz的有界窗口，页面只绘制上下行曲线。

P1 使用 ui.ini 保存主题/关闭偏好；UiState 只保留最多 32 份轻量筛选/滚动状态，不持有跨引擎 QML 对象。P2 的业务设置、事务和数据存储另行接入。

## 焦点与滚动布局约定

- 页面/弹框的滚动正文统一使用`ThemedScrollView { theme: ...; form: body; ColumnLayout { id: body; ... } }`；不自行设置body的x/y/width，不添加裸ScrollView。单正文可为Label、布局或文本区，implicitHeight必须反映其内容。安全区及稳定滚动条几何由组件负责（不按effectiveScrollBarWidth显隐切换宽度，避免换行高度测量反馈）；PageHost仅对页面根滚动表单使用pageLeftOutset/pageRightOutset外延至已有页边距，正文与页头对齐，窄窗需保留焦点与滚动条的安全余量。弹框留在自身边界内，不能仅给Dialog加padding或关闭clip来修复。
- 替换ScrollView的默认ScrollBar时必须同时保留样式的几何绑定：parent为外层ScrollView，竖条按mirrored定位边缘，y为topPadding、height为availableHeight，并保留横竖条active联动；不能让滚动条随正文移动或退化为左上角自然尺寸。回归须检查实际条的位置/高度，以及滚动位置和缩放后的同步，不仅检查正文padding。
- 页内虚拟列表/表格保留自己的滚动和焦点策略；页面级订阅列表的稳定滚动条外延至页边距，代理列表的滚动视口外延，标题和卡片宽度不含滚动条。嵌入式表格的滚动条仍在表格内部，不能为边线统一破坏回收或横向滚动。
- 普通控件沿用外扩FocusRing；导航、虚拟列表行与贴边节点背景设置`insetFocus: true`。水平表格按可见区设置focusClipLeft/Right；不要给每行增加Timer。
- 焦点尺寸只取ThemeTokens.focusExtent/focusMargin，圆角跟随表面；步进视觉不改变命中范围。新表单必须补真实聚焦状态及窄窗检查，`contracts`负责裸ScrollView源约束，`ui_accessibility`检查八页面/字体缩放安全区与焦点，`ui_subscriptions`检查真实弹框，`ui_custom_groups`保留步进/焦点回归。
- `Flow`只换行、不限制子控件宽度；其按钮/开关/选择器必须将width限制到可用宽度（如`Math.min(implicitWidth, parent.width)`），不能依赖Layout.fillWidth。横向工具栏按可用宽度与字号改为多行网格；可换行文字须提供fillWidth/minimumWidth及wrapMode，避免最小内容宽度反向撑破安全区。
