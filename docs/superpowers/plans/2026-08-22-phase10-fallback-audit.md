# Phase 10 fallback 审计与清理边界

日期：2026-08-22

## 目标

确认 Qt Quick/QML 主线与 React/QWebEngine 可选路径的功能、构建入口和发布边界，只有在真实双客户端验收通过后，才清理对应的 fallback 代码。

本切片只做审计和验证，不删除 QML、WebEngine 宿主或任何现有业务代码。

## 当前构建事实

| 路径 | 构建开关 | 当前用途 | 证据 |
|---|---|---|---|
| 默认 Qt Quick/QML | `LAN_CHAT_ENABLE_WEB_UI=OFF` | 默认/发布主线 | `client-cpp/gui/build/CMakeCache.txt` |
| QML 诊断构建 | `LAN_CHAT_ENABLE_WEB_UI=OFF`、`LAN_CHAT_ENABLE_PERF_OVERLAY=ON` | 性能采样测试 | `client-cpp/gui/build-bridge/CMakeCache.txt` |
| React/QWebEngine | `LAN_CHAT_ENABLE_WEB_UI=ON`、`LAN_CHAT_ENABLE_PERF_OVERLAY=OFF` | 可选 WebEngine 验证 | `client-cpp/gui/build-webengine-msvc-ninja2/CMakeCache.txt` |

`main.cpp` 当前仍以 QML 为默认入口；WebEngine 仅在显式启用时编译，加载失败时才回退到 QML。

## 功能对照

| 功能 | QML | React/WebEngine | P10 状态 |
|---|---:|---:|---|
| 远程服务器连接 | 有 | 有 | 自动化覆盖，待真实 TLS 双客户端 |
| 本地 Host 启动 | 有 | 有 | 自动化覆盖，待真实 Host/Guest |
| 局域网 Guest 加入 | 有 | 有 | 自动化覆盖，待真实 TLS 双客户端 |
| 群聊、私信、频道切换 | 有 | 有 | 待真实双客户端确认消息隔离 |
| 引用、复制、删除、撤回 | 有 | 有 | 前端测试通过，待真实服务端回归 |
| 表情输入、换行发送 | 有 | 有 | 前端测试通过 |
| 创建/删除/邀请/移除频道 | 有 | 有 | 前端和 bridge 测试通过，待权限回归 |
| 禁言/解禁、踢出成员 | 有 | 有 | 前端和 bridge 测试通过，待管理员回归 |
| 消息持久化/历史分页 | 有 | 通过 bridge 消费 | 待真实断线重连和历史回归 |
| 设置：深色主题/显示时间 | 有 | 有 | 可用性已覆盖 |
| 性能等级与图形信息 | 有 | 已通过 bridge 提供 | 已补齐，待真实运行验收 |
| Debug 性能面板 | 仅诊断构建 | 不进入发布 | 保持现状，不迁移到发布 UI |
| QWebChannel 失败错误页 | QML 回退 | 已提供受控错误页 | 已补齐，待发布包验收 |

## 阻断项

### 已修复：React 设置页功能不等价

React `SettingsPage` 现在通过 `ChatBridge` 的 `performance` 和 `graphics` 状态显示性能等级、有效等级、渲染 API、硬件/软件渲染、刷新率、DPI、FPS、P95 和最大帧耗时，并通过 `settings.setPerformanceMode` 修改 QSettings 持久化的性能等级。

性能信息仍由 Qt 侧采集，React 只消费稳定 JSON 字段，不直接访问 Qt 对象。

### 已修复：React 启动失败时使用 FakeBridge

`frontend/src/main.tsx` 现在只在 Vite 开发预览中允许 FakeBridge。发布 WebEngine 的 QWebChannel 不可用或初始化失败时显示受控错误页和重试入口，不再伪造可发送状态。

实现边界：

- FakeBridge 只允许测试/显式开发预览使用；
- 发布 WebEngine 在 bridge 初始化失败时显示受控错误页和重试入口；
- 不把连接失败伪装成可发送状态；
- QML fallback 是否保留由 Qt 宿主的显式启动策略决定，而不是由 React 静默伪造状态。

## 进入 fallback 清理的门槛

1. ~~React 设置页补齐必要的性能/图形信息。~~ 已完成。
2. ~~React/QWebChannel 失败路径不再使用发布态 FakeBridge。~~ 已完成。
3. ~~使用真实 TLS 服务端和两个客户端完成连接、中文群聊、私信隔离、离线私信、频道权限、管理员操作、撤回、断线重连和 CA 校验。~~ 已完成；同时覆盖第三个客户端的私信隔离与踢出验证。
4. 默认 QML、WebEngine 发布 qrc、Vite 开发三条路径分别完成启动和回归记录。
5. 确认性能 overlay 仍只存在于诊断构建，发布包不包含它。
6. 只有满足以上条件，才按页面逐步删除对应 QML fallback；未满足的页面继续保留。
