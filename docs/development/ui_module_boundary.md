# P1 平台壳与移动边界

桌面壳使用八个 moduleId 的左侧导航投影，工厂注册与导航位置无关。FeatureRegistry::project 可选取、去重和排序入口；resolve / resolveEmbedded 始终独立校验上下文，隐藏入口不能成为授权机制。

未来移动壳可选择 home/proxies/subscriptions/settings 等有限入口，其余已实现能力经 More / 明确路由访问，或用 proxy_summary 等内容工厂嵌入。More、底部导航、安全区、返回栈、触摸尺寸及移动平台适配均未实现，本期不按窗口宽度切换为移动 UI。

页面通过注入的 UiState 和 ThemeTokens 访问展示状态与路由意图；PageHost 不持有业务服务，完整页不能嵌入首页。EmbeddedHost 使用稳定 contentId / 独立 instanceKey，每个可见实例申请自己的需求，隐藏或销毁释放；参数或实例键变更先释放旧实例，无效上下文清空内容。P1 仅实现代理摘要这一代表内容。未来同模块多业务上下文须扩展状态键及有界投影，不能直接共享完整页对象。

P1 使用 ui.ini 保存主题/关闭偏好；UiState 只保留最多 32 份轻量筛选/滚动状态，不持有跨引擎 QML 对象。P2 的业务设置、事务和数据存储另行接入。
