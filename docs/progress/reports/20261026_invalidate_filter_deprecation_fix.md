# QSortFilterProxyModel::invalidateFilter 弃用修复记录

- 日期：2026-10-26（Asia/Shanghai）。
- 关联任务：P0-06；执行者：Zoo。
- 版本标识：D:/repos/quick-clash 当前未提交工作区增量；保留此前全部改动，不创建提交。
- 范围：仅替换 [runtime_list_model.cpp](../../../src/presentation/runtime_list_model.cpp) 中 4 处已弃用的 `QSortFilterProxyModel::invalidateFilter()` 调用，不修改设计、QML、依赖版本或其他源码。

## 1. 最新失败证据与原因

用户提供的 build/dev 日志显示，MSVC 编译 `src/presentation/runtime_list_model.cpp` 时在 247/267/274/318 行触发 C4996：`QSortFilterProxyModel::invalidateFilter` 已弃用，建议改用 `beginFilterChange()/endFilterChange()`。目标级 `/W4 /WX` 将警告升级为 C2220 错误，Ninja 停止，构建退出 2。

这是当前 Qt 头文件（6.12.0 msvc2022_64）对旧 API 的弃用阻断，不是逻辑错误；`invalidateFilter()` 的语义是立即按新过滤条件重算可见行。

## 2. 最小范围修复

在 [runtime_list_model.h](../../../src/presentation/runtime_list_model.h) 新增私有内联辅助 `refilter()`，用 `QT_VERSION` 条件编译封装版本差异：

- Qt ≥ 6.10：`beginFilterChange(); endFilterChange();`（官方推荐替代 API）。
- Qt < 6.10（含 6.8）：`invalidateFilter()`（该版本无弃用警告，可正常编译）。

将 [runtime_list_model.cpp](../../../src/presentation/runtime_list_model.cpp) 中 4 处调用点统一改为 `refilter()`：

1. `RuntimeListModel::setQuery`。
2. `RuntimeListModel::setSourceFilter`。
3. `RuntimeListModel::setLevelFilter`。
4. `RuntimeListModel::finishBatch`（仅在有过滤条件时）。

边界与影响：

- 两个分支语义等价：成对 `beginFilterChange()/endFilterChange()` 触发与 `invalidateFilter()` 相同的过滤重算与视图刷新；`filterAcceptsRow`、`lessThan`、排序与角色逻辑不变。
- 不关闭 /WX、不降低警告级别、不加弃用豁免宏；其余源码策略不变。
- 不改 QML、设计文档、依赖版本或构建配置；无新增运行时对象或进程。
- 恢复 Qt 6.8（<6.10）下的可编译性；6.8 分支未在本机实测，保持 NotRun。

## 3. 验证与未覆盖项

| 检查 | 结果 | 范围 |
|---|---|---|
| 用户原构建 | Fail | 用户日志退出 2；C4996×4 被 /WX 升级 |
| 修复后 `cmake --build build/dev --target quickclash`（Qt 6.12.0） | Pass | 通过 VsDevCmd x64 环境，`runtime_list_model.cpp.obj` 编译并链接 `quickclash.exe` 成功，退出 0 |
| Qt 6.8 分支编译 | NotRun | 本机无 Qt 6.8 环境；`#else` 分支调用 6.8 中未弃用的 `invalidateFilter()`，API 存在性经头文件核对 |
| 运行时行为/QML 渲染/内存预算 | NotRun | 本轮仅编译验收；过滤/排序运行行为与 ≤100 MB 目标仍待后续功能测试与实测 |

## 4. 后续

- 若后续 Qt 版本调整 `beginFilterChange` 签名（如增加变更范围参数），需复核本文件 4 处调用。
- 运行时过滤行为验证归入 P0-06 后续或相关功能测试任务。