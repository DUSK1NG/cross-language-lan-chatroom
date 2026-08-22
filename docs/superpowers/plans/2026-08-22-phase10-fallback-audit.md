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

## 已完成验证

- 前端 Vitest：10 个测试文件、59 个测试通过。
- 前端生产构建：TypeScript 检查和 Vite 构建通过。
- QML/诊断 CTest：8/8 通过。
- WebEngine CTest：9/9 通过，包含 `web-ui-host-tests`。
- Go 服务端单元测试：`go test ./...` 通过。
- 真实 TLS 验收：使用临时 SQLite 数据库和 `server-lan.crt`，P10 Alice/Bob/Charlie 三个协议客户端完成双客户端主流程与权限扩展验收。

前四项构建/单进程结果与真实 TLS 验收均已完成；局域网实体两台电脑验收仍应在发布前按 `docs/release-setup.md` 单独执行。

## 真实 TLS 验收记录

可复现脚本：[scripts/p10-tls-acceptance.mjs](../../../scripts/p10-tls-acceptance.mjs)。本轮使用重建后的 `server-go/chat-server.exe`、临时 `server-go/p10-acceptance.db` 和 `server-lan.crt`，验收结果为 `ok: true`：

- TLS 登录、管理员身份和 CA 校验；
- 中文群聊、私信投递与第三方隔离；
- 离线私信、重新登录和历史响应；
- 公共频道创建/加入/聊天；
- 私密频道拒绝、邀请、移除和删除；
- 管理员禁言/解禁、撤回、踢出与断开。

验收中发现并修复一个服务端时序缺陷：踢出通知原先在立即关闭 TLS 连接前可能丢失；现改为写泵完成通知后再关闭连接，并新增 Go 回归测试。

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
