# G3 Windows 日常使用交付与平台覆盖路线

日期：2026-10-25；负责人：Codex；关联任务：P3-12（方案交付）、P3-13（静态兼容待实施）、P3-07（G3汇总）。设计基线由1.7调整为1.8。保留已有未提交功能代码、任务历史和失败证据；本次交付范围为设计/计划文档及合并文档生成器摘要，不修改产品实现。

## 范围与验收决定

G3的主要开发、测试与验收目标统一为Windows端可日常使用。Windows实现同步保持共享架构和多平台功能边界，先静态核对，后续再逐个平台进行原生构建与真实系统覆盖。三平台指Windows/Linux/macOS，五平台在其基础上增加Android/iOS；移动宿主/官方内核资产、授权与分发可行性届时核验，不承诺桌面二进制直接用于移动，不为该计划自行编译/修改内核。

| 层次 | 本次方案规定的工作 | 当前结论 |
|---|---|---|
| Windows P3/G3 | 订阅解析/管理/计划更新/自动应用、选择组/节点与测速、模式、系统代理/TUN、真实节点转发、后台内核恢复与退出清理；八页面声明能力、使用说明和可恢复失败 | G3 NotRun，方案未替代功能验收 |
| 共享静态兼容 P3-13 | 平台接口/依赖、CMake、能力/错误、路径/凭据、导航/翻译、异步/宿主生命周期；修正已识别耦合 | Todo，初步发现如下问题，未标Pass |
| 桌面三平台 P4 | Windows正式安装/更新可先行；Linux/macOS分别原生构建、CI和系统功能，再汇总三平台Beta | 后续计划，未新增非Windows通过结果 |
| 远期五平台 | Android/iOS分别核验宿主/资产/权限/分发，再做最小隧道链路与完整体验 | 边界规划，未创建移动工程，原生/设备验收NotRun |

Windows普通用户干净包、关键失败恢复及TUN/服务实测仍保留。G2既有缺口不因方案调整关闭；G3汇总仍依赖G2有效通过。F17 IP卡仍为可选，未启用隐藏、启用后验收。Windows日常本地交付与正式公开发行的签名/在线更新分别记录。

日常代码交付一次Windows增量构建和受影响的功能/现有测试；资源敏感变更选一个代表场景做10—30秒短测，有效证据可复用。100,000,000B专用工作集上限不变，必要客户端/服务进程计入，总工作集与私有提交保留，Mihomo单列。无新故障时不增长时、全页面、帧/GPU或全平台矩阵。

## 静态兼容初步缺口

本次为有限源码阅读，不是完整静态检查通过报告。以下问题纳入P3-13，未修改其代码：

| 位置 | 已观察事实 | 后续修正 |
|---|---|---|
| src/application/desktop_runtime.h | 直接引用platform/windows的credential_store、system_proxy具体头 | 共享职责接口与Windows实现分开，由host/infra组装 |
| src/application/desktop_features.cpp | application中有windows.h/psapi、SCM服务查询与进程内存采样，虽有Q_OS_WIN保护 | 迁入平台资源/服务适配，经能力与值对象返回；宏保护不替代分层 |
| src/infra/core/engine_session.h | 公开头直接依赖platform/windows/owned_process.h | 抽离进程/会话契约，避免平台具体类型传播到共享调用者 |
| CMakeLists.txt | credential_store/owned_process/system_proxy的Windows路径源被直接列入库，部分系统库有WIN32条件 | 核对各源与PUBLIC依赖，按平台选择实现；未实现能力明确Unsupported，不以空成功占位 |

不要求为静态检查搭建空的全平台工程或提前写移动桥接；已有共享接口/模型测试可按变更复用。当前有条件宏也不证明其他平台可编译，更不证明系统功能可用。

## 文档及台账变更

- 01/README固定1.8版本、Windows G3目标、三/五平台路线和证据层次；02/11/15规定当前静态约束及未来平台替换点。
- 14明确必要Windows验收与测试开销；16更新ADR-02，增加ADR-25及风险/需求追踪；18新增P3/G3开发顺序和静态任务完成条件。合并生成器摘要同步Windows优先范围。
- TASKS新增P3-12方案和P3-13静态收敛；P3-01—09保留ID/状态，功能切片按实际已完成前置推进，G3汇总增加P2-17和静态任务，不能提前通过。
- P4-04收敛为Windows正式安装/更新，不再等待Linux/macOS；后者平台发行集成分别归P4-01/02，P4-05仍汇总三平台。移动任务在未来启动时再细拆。
- CHECKLISTS的G3-A—E改为Windows真实交付+静态兼容，另列静态执行项；进度README、报告入口和DEVLOG同步。P3当前3Done/1Doing/9Todo。

## 检查与结论

本次实际运行的检查如下，均为文档/台账检查：

| 命令 | 结果 |
|---|---|
| pwsh -NoProfile -File scripts/build_design_docs.ps1 | 退出0，19分册合并版重新生成，本地链接/锚点有效 |
| pwsh -NoProfile -File scripts/build_design_docs.ps1 -Check | 退出0，19分册、合并内容与本地链接一致 |
| pwsh -NoProfile -File scripts/check_progress.ps1 | 退出0，P3共13项（3Done/1Doing/9Todo），任务依赖/报告链接有效；G2 Blocked、G3 NotRun |
| pwsh -NoProfile -File scripts/check_source_layout.ps1 | 退出0，35目录/127文件命名符合规范；不是编译或运行验证 |
| git diff --check | 退出0，无差异格式错误；工作区存在既有LF/CRLF转换提示，不将其当产品错误 |

本次未运行产品构建、测试、网络请求、系统代理/TUN操作或性能采样，也未重新包装当前代码。方案通过不代表当前工作区产品通过；此前功能开发的contracts失败及未完成UI/包核对仍待处理，不能复用历史P3-11包直接宣布新实现完成。G2保持Blocked、G3保持NotRun。

下一功能任务P3-01完成当前测试与代理/订阅界面核对；P3-13先抽离上述具体依赖，再逐项补静态证据；P3-04真实Windows服务/TUN及P2-11/15/16恢复/干净环境缺口继续按必要条件关闭。
