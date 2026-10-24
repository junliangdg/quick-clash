# 仓库目录与命名统一

- 日期/执行者：2026-10-24 / Codex。
- 关联任务：P0-01/06/09；命名与路径维护，不改变 P0 功能完成状态。
- 请求：将本机 checkout 目录统一为 quick-clash；应用名为 QuickClash，内部 project/程序/命名空间为 quickclash。

## 范围

目标工作路径为 `D:/repos/quick-clash`。更新根 README、AGENTS、编码规范及构建说明的仓库名称和路径约定。源码、脚本和 presets 没有旧 checkout 路径的硬编码；现有未提交代码/文档、未跟踪报告、Git 历史和离线缓存随完整目录保留。源码子目录/文件仍使用小写下划线，C++ 标识符不能使用仓库名中的连字符。

旧构建输出包含原绝对路径，迁移后使用新的构建目录，不批量修改编译产物或假装原构建证据在新目录重新通过。已保存的内核探针日志/JSON 保持原样，其中的历史工作路径是当次实测事实；新运行从实际 checkout 路径生成记录。Git 远端 URL 指向外部仓库地址，须在远端仓库改名并核验后同步，不能用本机目录改名证明远端已改名。

## 历史尝试与当时状态

实际目录仍为 `D:/repos/clash-quick`。已核对这是普通主 checkout、`.git` 是目录、没有独立 worktree/core.worktree 路径设置，目标 `D:/repos/quick-clash` 不存在。完整工作区有此前未提交及未跟踪产物，重命名不应丢弃它们。

请求通过 PowerShell Rename-Item 改名时，自动审批服务返回 401 Unauthorized / 无可用账户凭据，动作未执行。目标目录位于现有 writable root 之外，需要通过该审批边界；没有绕过失败改走其他通道。目录改名和目标路径验证保持 Blocked。Codex 保存项目仍指向原 checkout，实际迁移后还须在应用中重新打开新目录；当前工具未提供更新保存项目路径的接口。

若在本机终端完成迁移，先关闭使用 checkout 的构建/IDE/终端，将终端工作位置切到父目录后执行：

```powershell
Set-Location D:/repos
if (Test-Path -LiteralPath D:/repos/quick-clash) { throw '目标目录已存在，停止改名' }
Rename-Item -LiteralPath D:/repos/clash-quick -NewName quick-clash -ErrorAction Stop
Set-Location D:/repos/quick-clash
git status --short
pwsh -NoProfile -File scripts/check_progress.ps1
pwsh -NoProfile -File scripts/check_source_layout.ps1
```

之后在 Codex/IDE 重新打开 `D:/repos/quick-clash`，从新目录继续开发。Git origin 仍是 `git@github.com:junliangdg/clash-quick.git`，尚无远端改名事实；不要提前改成不存在的 URL。代码和脚本的当前配置未发现旧产品名 ClashQuick/clash_quick 或旧目录路径硬编码；原始探针输出中的旧路径按历史证据保留。

进度/设计/命名检查不等于完整构建或 MVP 验收，原 P0 阻塞保持。

本次验证：`check_progress.ps1`、`check_source_layout.ps1`、`build_design_docs.ps1 -Check` 及 `git diff --check` 均通过；在 app/src/qml/scripts/CMake/CI 搜索旧名称没有匹配（rg 退出 1 表示未匹配）。再次核对源目录仍存在、目标目录不存在，没有报告目录迁移完成。

## 2026-10-24 P1 接续时的最新观测

实际 D:/repos/quick-clash 存在、旧 D:/repos/clash-quick 不存在，git status 可读取，HEAD/此前未提交修改与缓存保留。目录迁移已由会话外操作完成；上文“未执行”是此前尝试的历史，不能作为当前状态。Git origin 仍是原外部仓库 URL。

本会话可写范围仍指向旧目录，向新目录写 P1 文件的审批再次返回 401 无可用账户凭据，动作未执行。P1 代码/文档保存为允许写入工作区中的独立补丁，待重新打开新目录或恢复权限后应用。该权限故障不表示目录重命名未完成。

### 后续重试：工作目录与写权限已就绪

用户重新打开 D:/repos/quick-clash 并提供更新的工作区权限后，新目录已列入可写根。P1 补丁预检、应用均退出 0，后续直接在该 checkout 修订；目录迁移与写权限缺口已关闭。原生构建执行障碍和审批服务 401 是另一问题，见 [P1 当前报告](20261024_p1_applied.md)，不再将其写作仓库无法编辑。
