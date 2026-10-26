# 系统代理冲突显式开启重建基线（2026-10-26）

## 1. 问题与根因

用户反馈：外部程序修改系统代理后，开启系统代理开关报 `SystemProxyConflict`，开关打不开。

链路：

1. 租约层 [`ProxyLease::acquire()`](../../../src/platform/windows/system_proxy.cpp) 在当前系统代理值与租约记录的 `applied`/`before` 快照不一致时置 `conflict_` 并返回 `SystemProxyConflict`；
2. 运行时 [`DesktopRuntime::setSystemProxy(true)`](../../../src/application/desktop_runtime.cpp) 直接透传失败，开关关闭；
3. 唯一恢复入口是设置页按 `proxyConflict` 显示的 `reapply_system_proxy` 按钮（[`settings_page.qml`](../../../qml/features/settings/settings_page.qml)），用户难以发现。

## 2. 修复

`setSystemProxy(true)` 在 `acquire` 失败且 `conflict() && !active()` 时自动转 [`ProxyLease::reapply()`](../../../src/platform/windows/system_proxy.cpp)：把当前外部值快照为新 `before` 基线再申请租约。用户点击开关本身就是显式授权，不再失败关闭。

边界保留：

- 损坏租约文件仍报 `ProxyLeaseRecoveryRequired`，不自动基线化；
- `active()` 为真（本应用仍持有租约）不触发重开路径；
- 后台受管模式、`suspend`/`release`/`matches` 语义不变；
- 设置页 `reapply_system_proxy` 按钮保留，作为冲突状态的显式恢复入口。

## 3. 验证

| 命令 | 退出码 | 说明 |
|---|---|---|
| `cmake --build build/dev --target proxy_lease_test recovery_test`（VS DevShell x64） | 0 | 增量构建 |
| `build/dev/proxy_lease_test.exe` | 0 | 0.42 秒，租约意图/幂等/迁移/外部冲突/中断恢复通过 |
| `build/dev/recovery_test.exe` | 0 | 22.73 秒，含网络合并冲突后显式开关 re-baseline 及 release 恢复新外部快照 |
| `ctest --test-dir build/dev`（全量） | 8 | 29%：`provider_stream_test` 既有编译错误（`QHash::first()`，2026-10-26 DEVLOG 已记录为既有、非本轮引入）导致 16 项 Not Run；`ui_keyboard`/`ui_subscriptions*` 失败为字体目录/raise 平台问题，本轮未触碰 QML/UI |

测试调整：[`recovery_test.cpp`](../../../tests/integration/recovery_test.cpp) 的显式重开断言由 `reapplySystemProxy()` 改为直接 `setSystemProxy(true)`，验证开关路径自动 re-baseline 且 `proxyConflict` 清除，随后 release 恢复重开时的外部快照。

## 4. 状态与边界

- 专项状态：实现 Done，产品验证 Review（原生 PRECONFIG 转发未复测，MVP-05 保持 Stale）。
- 设计 10 第 3 节已同步稳定方案；TASKS 专项表、CHECKLISTS MVP-05 注记、DEVLOG 已同步。
- 未提交；未操作用户实例/真实订阅/TUN；GUI 主链路验收仍受既有 `provider_stream_test` 编译错误阻塞，非本轮引入。