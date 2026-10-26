# 任务与验收证据

2026-10-26工具维护：[依赖获取与Geo固定来源修复](20261026_dependency_fetch_fix.md)已完成。官方原快照与许可证实际字节匹配；11/11定向、完整在线/离线及YAML离线、语法/命名/冻结基线/差异检查通过。进度检查因既有历史包链接本地缺失Fail。原数据字节未变，未构建产品，不扩大阶段/MVP验收。

本轮功能修复：[代理出口与订阅更新内存](20261025_proxy_memory_fixes.md)。分组顺序、默认出口与查看语义、客户端/内核内存展示及更新缓冲已修复；7/7受影响回归、最终包真实GUI有变更新峰76.45MB专用工作集，原始公开夹具日志位于proxy_memory_evidence/。新包dist/proxy_memory_fixes，186文件摘要、随包smoke和两张中文实际配置截图通过。完整P3与G2/G3状态不变，旧包资源结论对新二进制Stale。下文为历史交付与范围记录。

当前范围评审：[G2必要/后移/移除](20261025_g2_scope_review.md)，设计1.9。G2本机最小闭环保留16项必要条件，干净普通用户环境及真实OS/GUI宿主补证归P3-14/G3；全协议握手、故障全排列和重复性能矩阵不再作为统一门槛。P2现21Done/2Review/1Todo，P3现3Done/1Doing/10Todo；G2修订范围NotRun，G3 NotRun，未新增产品Pass。下段恢复交付及后续报告保留当时状态，当前以TASKS/CHECKLISTS为准。

当前本机交付：[G2恢复与Windows包](20261025_g2_recovery.md)。P2-15 Done，首次失败/停止打断/真实worker强杀后Job、WinINet租约/事务恢复及当前runtime原生转发回归通过；P2为21Done/2Review/1Blocked。包dist/g2_recovery的186文件摘要和清Qt PATH smoke通过，单次10秒Ready首页专用工作集63.50MB且退出清理通过；真实OS事件/普通用户干净环境仍缺，G2 Blocked。逐项文档同步规则已写入开发规范。下文“最新”均为各历史切片的当时记录。

最新方案：[G3 Windows日常使用与平台覆盖路线调整](20261025_g3_windows_plan.md)。P3-12为文档方案Done，P3-13静态兼容Todo；三平台/五平台真实覆盖后置，G2/G3产品验收状态不变。该方案报告不替代当前功能代码与包的运行证据。下文为历史报告导航，当前状态以TASKS/CHECKLISTS为准。

当前最新：[窗口缩放与导航切换修复](20261024_resize_layout.md)。P2-24 Done；P2现20Done/2Review/2Blocked，G2仍Blocked。最终包dist/p2_resize，4/4受影响检查、真实窗口160步缩放及一次Ready首页内存短测通过，中文窄窗截图已核对；下文记录保留当时范围。

此前交付：[P2基础协议/自动组/具体诊断](20261024_p2_protocols.md)。P2-21—23 Done；当时包dist/p2_protocols_final和实际中文代理页核对记录保留，受本次变更影响的布局/图形/资源结论Stale。

更早交付见[响应式/语言/标准链接适配](20261024_p2_responsive_localization.md)：P2-18—20完成，7/7受影响检查、60.33MB专用工作集单轮短测；当时P2为16Done/2Review/2Blocked/3Todo。[功能计划](20261024_mihomo_feature_plan.md)中的基础协议/自动组现已完成，Geo/DNS仍按P3分期；私有订阅地址仅本地使用。

最新状态见 [P2功能交付](20261024_p2_implementation.md)：Windows订阅—官方内核—节点/模式—系统代理—受控HTTPS—停止恢复已实现及运行，13项Done、2项Review、2项Blocked，G2 Blocked、MVP-05/06 Pass；其余验收按具体缺口记录。P0/P1基线保留，资源只做必要单轮短测，未扩大长时/图形矩阵。[开发优先与G1范围重评](20261024_development_first.md)和[此前Windows证据](20261024_windows_private_working_set.md)保留历史；旧原型记录不直接证明新增业务页。

最新接续见 [2026-10-24 P0 验证报告](20261024_p0_validation.md)：构建/探针超时与日志、进程树测试、API 重跑及审批服务故障，逐次原始证据保留在独立目录。

仓库命名及工作目录迁移见 [目录迁移记录](20261024_repository_rename.md)，本轮已观测实际目录改名完成，历史审批失败仍保留。

模板见 [任务报告](../templates/task-report.md) 和 [验收报告](../templates/acceptance-report.md)。复制后填写事实再链接到台账；不能将模板路径直接当作完成证据。

推荐命名为 `YYYY-MM-DD-P0-01-toolchain.md`、`YYYY-MM-DD-G0-baseline.md`、`YYYY-MM-DD-MVP-06-forwarding.md`。一次完整运行可证明多个验收项，但报告必须分别列出 ID、条件、断言和结果；每行可引用同一报告，不重复复制日志。后续重跑新增日期/序号，保留旧报告。

报告至少标识源码/补丁、构建、Qt/Mihomo/OS 版本与输入摘要，并链接仓库内的脱敏输出或可核验的 CI run。大体积原始数据/安装包可存构建设施，报告必须记录构建 ID、不可变版本/摘要与获取位置，不能只写“本机测试通过”。本地证据路径以报告目录为基准；链接文件必须实际存在。

不提交真实订阅URL、节点凭据、控制secret、非公开私钥、未经清理的连接目标和用户网络快照。tests/fixtures/network中的证书/私钥为刻意公开的本地TLS测试数据，仅供自动化测试，禁止用于真实服务；这是公开夹具例外，不允许保存用户秘密。保留足以核对测试的脱敏内容、输入摘要与恢复断言；报告“无泄漏”需实际检查。

当前 [P1 落地实现与验收缺口](20261024_p1_applied.md)：源码和文档已写入实际 checkout，18 个 QML 静态检查通过；构建在 MSVC ABI 检测超时、沙箱外重试因审批服务 401 未执行，实际运行与资源仍未验收。[此前补丁记录](20261024_p1_implementation.md)保留权限恢复前的历史。

最新 [AutoUic 误扫描修复](20261024_autouic_fix.md)：用户的 build/dev 构建已进入源码阶段，但主程序与状态测试的自动生成失败；关闭不需要的 Designer 扫描，按用户约定不构建，修复后的原生验收仍待运行。

后续 [QML 缓存生成代码警告修复](20261024_qmlcache_warning_fix.md)：最新用户构建已进入 QML 缓存编译，Qt 6.8.0 生成代码触发 MSVC C4702 并被 /WX 阻断；仅为缓存生成 C++ 文件添加警告豁免，保留自有源码严格策略，按约定不构建。
