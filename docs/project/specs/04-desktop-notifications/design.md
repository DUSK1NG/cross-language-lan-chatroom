# 04 桌面通知与托盘 — 设计

## D1 C++
- `client-cpp/gui/src/notifications/notification_center.{hpp,cpp}`：持有 `QSystemTrayIcon`；接收 `GuiChatController` 的「新消息」「新审批」信号；判断窗口激活状态、免打扰、静音、隐私级别；3 秒合并窗口（按会话键）；`showMessage()` 发出通知并记录 `lastNotifiedConversation`；`messageClicked` → 激活窗口 + 发出 `openConversationRequested(kind, id)`。
- 纯逻辑（是否通知、合并、文案）抽成 `NotificationPolicy`（无 Qt GUI 依赖），可在 `lan_chat_core`（规格 01）中用 QtTest 覆盖。
- 关闭行为：当前顶层窗口是 `main.cpp:73` 的裸 `QWebEngineView`，没有窗口类。新增 `MainWindowFilter`（`QObject` 事件过滤器，安装在该 view 上）拦截 `QEvent::Close`：「房主模式 && closeToTray」时 `ignore()` + `hide()`，否则放行。同时设置 `QApplication::setQuitOnLastWindowClosed(false)`，由托盘「退出」显式调用 `qApp->quit()`；退出路径复用现有 `GuiConnectionWorker::stopHostedServer()`（`gui_connection_worker.hpp:141`）。
- 设置持久化：`QSettings`（与现有性能/日志设置同处），通过 bridge 状态 `notifications: { enabled, privacy, dnd, mutedRooms[], closeToTray }` 发布。

## D2 bridge
- 命令：`settings.setNotifications {enabled?, privacy?, dnd?, closeToTray?}`、`room.setMuted {room, muted}`（白名单校验）。
- C++ → 前端：激活会话通过现有 `conversation.select*` 路径完成，不新增前端事件。

## D3 前端
- 设置页「通知」分组（开关、隐私三选一、免打扰、房主关闭到托盘）。
- 侧栏频道项右键/悬停菜单「静音」；静音图标使用 `Icon`（新增 `bellOff`）。

## 测试
- QtTest：`NotificationPolicy` 的激活/免打扰/静音/审批例外/隐私文案/合并窗口。
- vitest：设置页分组与命令派发；静音图标显示。
- 人工：验收清单 1–4。
