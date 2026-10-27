# 方案 C 统一视觉设计与概念预览

- 日期：2026-10-27；执行者：Codex；关联任务：P0-05、P3-15，后续产品迁移P3-16；工作包W01/W05。
- 设计基准：1.10；工作区文档补丁，未提交。起始工作区仅包含本任务已修改的设计17、P3-15任务行和预览文件；产品源码/QML未修改。
- 交付结论：P3-15 Done；规范、静态概念图及离线交互预览完成，浏览器与最终文档检查通过。产品迁移、构建/CTest及资源测量NotRun。

## 1. 当前交付

[设计17](../../design/17-界面参考与QtQuick落地.md)完整定义方案C的视觉原则、应用壳/断点、八页面、语义色/状态色、字体/尺寸/动效、共享组件、可访问性、Qt Quick边界及REF-01—16。设计入口及01/04/08/13/15/16/18同步使用该规范；合并版由脚本生成。

[离线概念预览](../../reference/quickclash_minimal_preview.html)与[可编辑源](../../reference/quickclash_minimal_preview.fragment.html)固定为方案C，支持Light/Dark/跟随系统、八页面、控件状态、节点筛选/选择、端口校验、菜单/确认及焦点返回。[Light](../../reference/quickclash_minimal_light.png)/[Dark](../../reference/quickclash_minimal_dark.png)概念图嵌入设计正文。源由Python标准库[脚本](../../../scripts/build_ui_preview.py)导出，无网络依赖，CSP禁止网络连接。

按用户要求直接采用新设计，移除旧界面截图、参考台账、专门的旧布局报告及相应日志/任务链接，不建立旧方案归档；混合工程报告中的旧UI参考行已移除。其余内核、构建、功能、资源原始证据未改变，不将其改写成新视觉的通过证据。

## 2. 验证记录

| 检查 | 实际结果/边界 |
|---|---|
| 浏览器Light/Dark、八页面切换 | Pass；页面标题/内容与导航匹配，截图核对两主题；演示不调用产品服务 |
| 系统跟随、字段校验、筛选/选择 | Pass；系统选择解析为本机浅色；70000拒绝/7890显示应用确认；无匹配空态、日本节点筛选与选中 |
| 菜单/弹层 | Pass；Escape关闭菜单并恢复触发器焦点；确认弹层默认焦点取消，关闭恢复删除触发器 |
| 360×480预览 | Pass；四个底部入口，DOM无横向溢出；实际浏览器滚动条占15px，内容345px。非Qt窗口验收 |
| 599/600、999/1000、1280等断点/明暗页面 | Pass；360/599/600/999/1000/1280宽×两主题×首页/代理/设置/控件48项，无横向溢出，窄窗四入口/其余八入口 |
| 颜色对比度/脚本语法/离线依赖 | 退出0；74组对比度全部达标，JS语法通过，无远程资源/API依赖，固定方案C，每主题一套色板 |
| python scripts/build_ui_preview.py 及 --check | 均退出0；自包含导出与源一致 |
| pwsh -NoProfile -File scripts/build_design_docs.ps1 及 -Check | 均退出0；19章/合并内容/锚点/本地链接一致 |
| pwsh -NoProfile -File scripts/check_progress.ps1 | 退出0；任务/依赖/报告链接有效，G1 Stale、MVP 0/16 |
| pwsh -NoProfile -File scripts/check_source_layout.ps1 | 退出0；36目录/166文件命名通过，非产品编译 |
| git -c safe.directory=D:/repos/quick-clash diff --check | 退出0；仅LF→CRLF提示 |
| 产品QML迁移、Windows构建/CTest、实际字体/DPI、多屏与资源 | NotRun；本轮纯设计/预览，不运行产品构建 |

检查中修正了浅色危险按下态4.429:1的问题（改后达标），以及浏览器缩放造成的分数像素断点。首次使用Windows PowerShell 5.1解析UTF-8脚本失败，按仓库入口改用pwsh 7.6.5后生成与检查通过。初次进度检查只有已存在的旧包链接缺失；将该报告链接改为包缺失/Stale说明后通过，没有重建或伪造产品包。

本次检查及文件摘要保存在[证据目录](unified_visual_design_evidence/README.md)，包含[命令输出](unified_visual_design_evidence/checks.log)、[浏览器结果](unified_visual_design_evidence/browser_checks.json)、[对比度结果](unified_visual_design_evidence/contrast_checks.json)和[文件摘要](unified_visual_design_evidence/asset_sha256.json)。浏览器预览通过只覆盖示例，不能将REF-01—16全部标为产品Pass。

## 3. 当前产品与接续

G0工程事实保持Pass；G1因视觉基准变化标Stale，撤销G1-B。有效生命周期/资源技术证据按原版本保留，不支持方案C已落地的结论。G2/G3仍NotRun，MVP Pass=0/16，MVP-05/06仍Stale；内存异常未因本次视觉设计关闭。

P3-16（Todo）按设计17收敛ThemeTokens、共享控件和八页面，继续使用既定系统字体/Unicode符号与Qt跨平台渲染；需一次Windows增量构建、受影响功能/现有测试、真实主题/字段/弹层/窄窗核对。资源敏感交付选单场景10—30秒短测，不用概念数字证明≤100MB。P3-01保留Zoo负责，实际代理组/测速功能验收仍需完成；本轮未改变产品实现。
