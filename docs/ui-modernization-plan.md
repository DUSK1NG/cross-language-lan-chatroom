# LAN Chat 现代 UI 与性能架构升级实施计划

**目标：** 在保留 Go TLS/TCP 服务端、C++ 网络协议与现有聊天业务的前提下，逐步把当前 Qt Quick/QML 桌面客户端升级为可观测、可降级、低延迟的现代桌面聊天应用。

**架构：** 以现有 Qt 6 Quick/QML GUI 为主线，C++ 通过 `GuiChatController` 暴露业务状态和命令，网络由 `GuiConnectionWorker` 在线程中处理。React/Vite + QWebChannel 是可选实验路径，不作为第二套默认 UI 扩张；QML 迁移完成并验收前保留现有回退路径。

**技术栈：** Go 1.25 TLS/TCP/SQLite、C++20、Qt 6.11 Quick/QML/QuickControls2、Qt Scene Graph/RHI、CMake/Ninja、现有 QAbstractListModel、可选 React/Vite/QWebEngine/QWebChannel。

**规范来源：** `C:\Users\jking1\.codex\attachments\083bc095-c5f5-4fef-aaa8-a3cb56f94762\pasted-text.txt`

## 全局约束

- 尽可能保留现有 C++ / Qt 后端业务逻辑。
- Windows 优先使用 Qt 6 默认图形后端；不强制 Vulkan，不引入 CUDA、ROCm、OpenCL、自定义 D3D12/Vulkan renderer 或 GPU 型号硬编码。
- UI 与网络、TLS、证书、线程和数据库之间只能通过控制器/模型/桥接接口通信；QML 不直接访问 socket。
- 动画优先使用 `opacity`、`scale`、`x/y transform`，避免在动画过程中修改布局尺寸、anchors、margin 和复杂文本度量。
- 保留 QWidget/QML fallback 和可选 WebEngine 路径，直到对应替代页面完成真实双客户端验收。
- 正确性 > 稳定性 > 响应性 > 可维护性 > 视觉效果。
- 每个阶段都必须编译、测试、启动验证；不进行一次性大规模重写。

## Phase 0：当前架构分析（已完成）

### 1. 实际目录与模块

```text
LAN Chat
├── server-go/                    Go TLS/TCP 服务端
│   ├── main.go                   参数、监听、证书和数据库启动
│   ├── client.go                 登录、读写泵、客户端生命周期
│   ├── hub.go                    Hub goroutine、广播、用户和频道状态
│   ├── protocol.go/message.go     4 字节大端长度帧和 JSON 消息
│   ├── auth_store.go              SQLite 账户和密码校验
│   └── *_test.go                  协议、TLS、Hub、账户和消息测试
├── client-cpp/                   C++ 协议客户端和测试
│   ├── include/                  配置、认证、连接、协议和消息接口
│   ├── src/                      Winsock2/OpenSSL/TLS 客户端实现
│   └── tests/                    协议、连接、命令和认证测试
├── client-cpp/gui/               Qt 6 C++20 GUI
│   ├── src/
│   │   ├── main.cpp              QGuiApplication + QQmlApplicationEngine
│   │   ├── gui_chat_controller.*  QML-facing state/command controller
│   │   ├── gui_connection_worker.* worker-thread network and Host process
│   │   ├── chat_model.*           QAbstractListModel implementation
│   │   ├── chat_bridge.*          optional React/WebEngine state/command adapter
│   │   └── bridge_protocol.*      optional bridge command/error contract
│   ├── qml/                      Main、页面、控件、组件、主题和 SVG 资源
│   ├── resources/frontend.qrc.in  bundled React release resource template
│   └── tests/                    Qt Test bridge/controller seam tests
├── frontend/                     optional React/Vite WebEngine UI
├── docs/                         protocol、架构、测试、发布和迁移文档
└── scripts/                      Windows 启动、构建和发布脚本
```

### 2. 当前数据和线程链路

```text
Go TLS/TCP Server
  -> GuiConnectionWorker::receiveLoop() [std::thread]
  -> queued signal
  -> GuiChatController [GUI thread state/model mutation]
  -> ChatListModel [QAbstractListModel]
  -> QML ListView / Repeater
```

GUI 入口默认加载 `LanChatGui/Main`。只有启用 `LAN_CHAT_ENABLE_WEB_UI=ON` 时，才会编译可选的 WebEngine/QWebChannel 路径；发布默认不依赖 Node，React 资源通过 qrc 嵌入。

### 3. 可以原样保留的业务逻辑

- Go 的长度帧、TLS、Hub goroutine、认证、频道权限、私聊和离线消息逻辑。
- C++ `auth`、`connection`、`message`、`protocol` 的网络协议和 TLS 接口。
- `GuiConnectionWorker` 的网络接收线程、Host `QProcess` 和 queued signal 返回方式。
- `GuiChatController` 暴露的聊天命令语义和 `ChatListModel` 的现有角色名称。

### 4. 当前 UI 与业务耦合

- QML 通过全局 context property `chatController` 调用 `Q_INVOKABLE`，不直接访问 socket，方向正确。
- `GuiChatController` 同时负责会话状态、QSettings 连接记忆、模型创建、消息分发和部分权限判断，属于当前最大的 UI/业务聚合模块；后续应通过内部深模块逐步拆分，不先改 public QML contract。
- `ChatBridge` 将多个 Qt 模型序列化成单个完整 JSON snapshot；该路径目前是可选 React UI 的兼容接缝，不应继续扩大 snapshot 负担。

### 5. 主要性能风险

1. `ChatListModel::append()` 每次插入都发送一组 rowsInserted；`users_response` 和 `rooms_response` 会在 GUI thread 中清空并逐行重建模型，用户较多时会产生大量 QML binding 更新。
2. `ChatListModel` 使用 `QList<QVariantMap>` 并通过线性扫描查找消息、房间和用户；当前每个模型有 1000 行上限，能够防止无限增长，但没有历史分页语义。
3. `ChatPage.qml` 通过 `Loader` 页面切换；从设置返回聊天会销毁并重新创建整个 ChatPage、ListView 和可见 delegate，可能造成首帧创建尖峰。
4. 消息列表已经使用 `ListView`，并配置 `cacheBuffer`，这是正确方向；但没有向上分页、滚动位置保持和大规模消息 fixture，无法证明 10,000/50,000 条历史的行为。
5. QML 视觉效果主要是渐变、透明度和轻量 Behavior；当前没有大面积 Blur、每条消息实时阴影或 ShaderEffect，GPU 风险相对可控。后续不得无 profiling 添加这些效果。
6. 控制器析构仍需要等待 worker 完成 socket/process 收尾；交互式 `disconnectFromServer()` 已改为 queued 异步调用，避免异常网络状态阻塞 GUI。
7. 自绘无边框窗口使用 `FramelessWindowHint` 和 `startSystemMove()`，需要在高 DPI、最大化、Snap 和多显示器环境进行人工验收。
8. 主题 token 已存在，但目前是固定深色；设置页中的主题开关和完整 System/Light/Dark 语义尚未完成。
9. 当前没有 GraphicsInfo、性能模式、FPS/Frame Time overlay 或 QSG 诊断入口；性能优化缺少可观测性。
10. 当前仓库没有 UDP discovery、文件/图片/音视频传输实现；本计划不虚构这些模块，只保留后续扩展位置。

### 6. 已知实现边界

- 默认 GUI：Qt Quick/QML；不是 QWidget。
- 可选 GUI：React/Vite + Qt WebEngine + QWebChannel，当前为兼容/实验构建路径，不取代默认 QML。
- 当前消息模型最大保留 1000 行，ChatBridge snapshot 最多序列化 500 行；这不是完整历史分页。
- 当前已有 Go、C++ 协议、Qt bridge 和 React 测试，但缺少 QML 交互自动化、真实 GPU frame-time 基线和大消息量 fixture。
- 工作树已有用户修改、构建目录、缓存和临时可执行文件；后续只修改明确列出的源文件，不清理或重置无关内容。

## 目标架构

```text
Qt Quick/QML UI
  -> small QML-facing interfaces
  -> GuiChatController / GraphicsInfo / model adapters
  -> worker-thread network and services
  -> C++ protocol/TLS
  -> Go TLS/TCP Hub

QML scene graph
  -> Qt RHI
  -> Qt-selected Direct3D 11 / other supported backend
  -> Windows-selected AMD / NVIDIA / Intel adapter
```

React/WebEngine 作为可选适配器继续使用 `ChatBridge`，但不与 QML 同时承担默认 UI。任何新 UI 能力优先落在 QML 主线；React 仅在已有 optional build/test 覆盖范围内同步必要的 bridge contract。

## 迁移方案与顺序

1. **Phase 0：分析与可观测性计划** —— 本文档和风险清单。
2. **Phase 1：Qt Quick 基础能力补齐** —— GraphicsInfo、QML context 接缝、设置页图形信息、QSG 调试文档和最小测试。
3. **Phase 2：主窗口和导航保持稳定** —— 将页面 Loader 切换抽象成可保留页面状态的导航模块，先解决设置返回的挂载尖峰，再统一页面过渡。
4. **Phase 3：Sidebar/Conversation List** —— Repeater 只保留低基数侧栏；如规模增长，增加模型过滤和批量更新接口。
5. **Phase 4：MessageModel/ListView** —— 增加消息角色模型、增量插入、历史分页、滚动位置保持和 100/1,000/10,000/50,000 条 fixture。
6. **Phase 5：Input Bar** —— 保留输入低延迟，完善文件/Emoji 接口占位，不把业务计算放进 QML JavaScript。
7. **Phase 6：Popup/Toast/Dialog** —— 统一 Popup/Dialog 的 transform/opacity 动画、减少重复实现和层级。
8. **Phase 7：线程优化** —— 将历史读取、文件、图片、数据库和重 JSON 处理移到 worker/线程池；保持 QObject 所有权和 queued connection 清晰。
9. **Phase 8：Profiling** —— QSG_INFO、QSG_VISUALIZE、QML Profiler 和 Debug-only performance overlay，记录 P95/P99/max frame time。
10. **Phase 9：性能等级** —— Automatic/High/Balanced/Power Saving；根据渲染 API、软件渲染、刷新率和实际 frame time 调整效果，不根据 GPU 名称硬编码。
11. **Phase 10：清理 fallback** —— 只有 QML/业务双客户端验收完成后，才评估移除旧/可选 UI 代码。

## Phase 1 具体执行计划（本轮）

### Task 1：GraphicsInfo 深模块

**目标：** 只读地暴露 Qt Scene Graph/RHI 和当前屏幕信息；任何信息不可用时返回 `Unknown`，不能成为启动硬依赖。

**文件：**

- Create: `client-cpp/gui/src/graphics_info.hpp`
- Create: `client-cpp/gui/src/graphics_info.cpp`
- Modify: `client-cpp/gui/CMakeLists.txt`
- Modify: `client-cpp/gui/src/main.cpp`
- Test: `client-cpp/gui/tests/graphics_info_tests.cpp`

**接口：** `GraphicsInfo` 继承 `QObject`，提供 `graphicsApi`、`renderer`、`vendor`、`hardwareAcceleration`、`softwareRendering`、`refreshRate`、`dpi`、`resolution` 属性和 `refresh()`/`attachWindow(QQuickWindow*)` 方法。公共接口只返回稳定字符串、布尔值和数值；不把 QRhi、D3D、OpenGL 对象暴露给 QML。

**实现边界：** 通过 `QQuickWindow::rendererInterface()->graphicsApi()` 获取图形 API；通过 `QScreen` 获取刷新率、DPI 和分辨率；renderer/vendor 在 Qt 公共 API 无法可靠获取时显示 `Unknown`，不引入显卡厂商专用 API。

**验证：** Qt Test 覆盖默认值、`Unknown` 降级和属性可读；GUI 启动后设置页能显示信息；QML 不依赖具体 GPU 型号。

### Task 2：QML context 接缝和设置页信息

**目标：** 将 `GraphicsInfo` 注入现有 QML engine，并在设置页增加“性能 / 图形信息”只读区；不修改聊天命令和网络协议。

**文件：**

- Modify: `client-cpp/gui/src/main.cpp`
- Modify: `client-cpp/gui/qml/pages/SettingsPage.qml`
- Modify: `client-cpp/gui/qml/theme/Theme.qml`（仅复用现有 token，必要时补充性能模式 token）
- Modify: `client-cpp/gui/CMakeLists.txt`（如需新增源文件）

**验证：** QML 模块构建通过；启动、进入设置页、返回聊天均不改变连接状态；信息缺失时显示 `Unknown`。

### Task 3：调试入口文档

**目标：** 记录 QSG_INFO、QSG_VISUALIZE、QML Profiler 和默认/可选构建路径，形成后续性能基线入口。

**文件：**

- Modify: `docs/testing.md`
- Modify: `docs/react-webengine-testing.md`（只补充 QML/WebEngine 的性能边界）

**验证：** 文档命令使用当前实际 build 目录和可用 Qt 版本，不假设系统 PATH 中已有 Node 或 Qt。

## 后续阶段的准备修改文件

- Phase 2: `client-cpp/gui/qml/Main.qml`、页面导航模块、`client-cpp/gui/qml/theme/Theme.qml`、QML 回归测试入口。
- Phase 3: `client-cpp/gui/src/chat_model.*`、`ChatPage.qml`、`RoomItem.qml`、`DirectMessageItem.qml`。
- Phase 4: `chat_model.*`、`ChatPage.qml`、`MessageDelegate.qml`、`GuiChatController::handleMessage`、历史分页测试。
- Phase 7: `gui_connection_worker.*`、`gui_chat_controller.*`、新增 worker/service 模块及线程生命周期测试。
- Phase 8/9: `graphics_info.*`、性能采样模块、Settings 页面和 Debug-only overlay。

## GPU 渲染方案

- 不设置固定显卡型号分支，不引入 CUDA/ROCm/OpenCL。
- 让 Qt Quick 使用 Qt 6 Scene Graph/RHI 的平台选择；Windows 首选 Qt 默认后端，除非 profiling 证明需要显式配置。
- 只在 GraphicsInfo 中观测 API 和软件渲染状态；GraphicsInfo 失败不会阻止启动。
- 大面积 Blur、每条消息实时阴影、持续 ShaderEffect 只允许在 profiling 后评估；Power Saving 模式必须能关闭这些昂贵效果。

## 线程模型方案

- GUI thread：QML 状态、输入、模型通知和轻量信号处理。
- Worker thread：TLS/TCP 接收、发送、连接重试和 Host 进程控制；当前 `GuiConnectionWorker` 已具备基础隔离。
- 后续 worker/service：文件 IO、图片解码/缩放、数据库查询、历史分页、压缩、加密和大规模 JSON 转换。
- Worker 不直接操作 QML 对象；跨线程只使用 queued signals/slots 或明确的 invokable seam。
- `BlockingQueuedConnection` 只保留在有界且已验证的关闭路径；后续改为可取消、可观察的异步关闭。

## 风险与回滚

| 风险 | 预防 | 回滚 |
|---|---|---|
| GraphicsInfo 在不同 Qt backend 上取不到信息 | 所有字段允许 `Unknown`，不读取私有 Qt API | 移除 context property 和设置页信息区，聊天逻辑不受影响 |
| QML 页面迁移破坏连接状态 | 每页完成后保留 `GuiChatController` 和 QML fallback | 恢复原 Loader/QML 页面，不动 controller/worker |
| 模型批量更新改变消息顺序 | 先加测试再改 beginReset/增量接口 | 回退到现有 rowsInserted 实现 |
| WebEngine 和 QML 双路径漂移 | WebEngine 保持 optional，只同步 bridge contract | 关闭 `LAN_CHAT_ENABLE_WEB_UI`，默认 QML 独立运行 |
| GPU/驱动异常 | Qt 默认 RHI、软件渲染检测、Unknown 降级 | 关闭高级效果并保留基础 QML 控件 |
| 高 DPI/无边框窗口行为回归 | 保留系统移动/最大化接口，加入手工矩阵 | 回退窗口框架和页面动画，不删除系统窗口功能 |

## Phase 1 完成判定

- `GraphicsInfo` 编译并有 Qt Test 覆盖。
- QML engine 可启动，设置页显示 Graphics API、硬件/软件渲染、刷新率、DPI、分辨率等信息。
- GPU 信息缺失显示 `Unknown`，不影响聊天启动。
- Go、C++ 协议、Qt bridge 和前端既有测试保持通过。
- GUI 可启动，进入设置页再返回聊天，连接和消息模型不重置。
- 未引入 GPU 型号硬编码、固定 16ms 刷新器、Electron 或第二套默认 UI。

## Phase 2 执行记录：缓存式页面导航与批量模型基础

本阶段先处理设置页返回聊天时的挂载尖峰，并同步落地后续列表优化需要的模型批量接口：

- `qml/Main.qml` 从单一动态 `Loader` 改为缓存式页面 Loader。模式页、连接页、本地 Host、设置页和聊天页各自只创建一次；切换设置/聊天时不再销毁并重建 `ChatPage`、`ListView` 和消息 delegate。
- 导航切换暂不叠加新的复杂过渡动画，优先保证页面复用和输入响应稳定；统一过渡动画放到后续 Popup/导航阶段。
- `ChatListModel` 新增 `appendRows`、`replaceRows`、`updateRows`，将房间、成员、私聊列表刷新从逐行结构通知改为批量通知，并继续保留 1000 行上限。
- `users_response` 和 `rooms_response` 使用批量替换；成员数更新使用单次范围 `dataChanged`，不改变协议字段和业务语义。
- 新增 `chat-model-tests`，并增加控制器级测试，锁定用户/房间刷新不会退化为逐行模型更新。

本阶段仍未处理历史消息分页、线程池、文件传输或 GPU 型号识别；这些保持在后续阶段，避免把未验证的业务和渲染变化混入导航优化。

## Phase 4 当前执行切片：消息列表基础与历史接缝

当前仓库的 Go 协议没有公开频道消息历史请求/响应，服务端也没有保存公开频道消息的持久化表。因此本切片只实现已经有真实协议基础的部分，不伪造历史分页已完成：

- `ChatListModel` 新增 `prependRows()`，为后续历史分页插入旧消息提供模型接缝，并继续执行 1000 行有界策略。
- 新增 100、1,000、10,000、50,000 条批量 fixture，验证大批量输入不会突破模型上限。
- `ChatPage.qml` 的消息 `ListView` 增加底部跟随策略：用户在底部时跟随新消息，用户上滑查看旧消息时保持视口；头部插入时按内容高度差补偿 `contentY`。
- 发送消息时显式恢复底部跟随，切换会话时重新定位到当前会话末尾。

Phase 4 后续仍需单独设计并实现服务端消息存储、`history_request/history_response` 协议和客户端分页状态，完成后才能宣称完整历史分页。

## Phase 3 执行记录：侧栏会话过滤代理

- 新增 `ConversationFilterModel`，基于 Qt `QSortFilterProxyModel`，只在模型层保留匹配的频道或私信行。
- 频道搜索使用 `roomName`；私信搜索同时使用 `displayName` 和 `userCode`，查询统一 trim 并大小写不敏感。
- `GuiChatController` 暴露过滤后的频道/私信模型和 `setSidebarQuery()`，QML 不再为不匹配项创建隐藏 delegate。
- `ChatPage.qml` 保留现有低基数 `Repeater` 和选择行为，只替换数据源并移除 QML 侧逐项 `visible` 过滤。
- 新增 `conversation-filter-tests`，覆盖空查询、单字段、多字段和无匹配场景。

## Phase 4 持久化完成记录（2026-08-22）

历史消息支持：

- Go SQLite 新增 `chat_messages`，每个会话最多保留 10,000 条消息。
- `history_request` / `history_response` 支持房间和私信的游标分页，单页最多 100 条。
- 服务端在实时广播前生成并保存消息 ID 与时间。
- 撤回使用墓碑状态，重新加载后显示“消息已撤回”并保留消息 ID。
- 房间可见性和私信双方隔离由服务端校验。
- Qt 客户端切换会话时加载最新页，向上滚动时前置旧消息，并按消息 ID 去重，保持 ListView 视口稳定。

## Phase 5 开始：输入栏

Phase 5 首个切片保持文本和表情输入在低延迟 GUI 路径上；本项目暂不加入文件选择、上传或附件持久化功能。

本轮 Phase 5 已完成输入栏首个切片：

- 单行 `TextField` 改为受控高度的多行 `TextArea`，最多增高到 116px。
- Enter 发送，Shift+Enter 换行，保留鼠标选择和表情插入。
- 输入框高度随内容变化，避免长文本挤压消息列表。
- 增加轻量字符计数提示；不在 QML 中执行网络或数据库业务。

## Phase 6 首个切片（2026-08-22）

- 新增 `AppPopup`，统一普通弹层的淡入/淡出与轻量缩放动效。
- 新增 `AppToast`，支持非阻塞、自动关闭的连接错误提示。
- 成员列表、用户资料、表情面板和消息操作菜单复用统一弹层基类。
- `AppDialog` 保留模态确认语义，并统一变换原点和动效节奏。

## Phase 7 首个切片（2026-08-22）

- 历史消息 SQLite 查询从 Go Hub 主循环移到有界 worker。
- Hub 只负责客户端存活检查、房间/私信权限校验、任务排队和结果投递。
- worker 不访问 Hub 的客户端或房间 map，查询结果通过 `HistoryResults` channel 回投。
- 单 worker 限制数据库并发压力；队列满时返回明确的服务繁忙错误。
- GUI/TLS 收发线程边界和现有历史分页协议保持不变。

## Phase 7 第二个切片（2026-08-22）

- GUI 用户触发的断开连接改为 queued 异步调用，不再在主线程等待网络 worker。
- 控制器析构时仍执行一次有界收尾，确保 socket、接收线程和本地 Host 进程释放干净。
- 连接、重连和断开请求继续按 worker 线程事件队列顺序执行，避免并发访问连接状态。

## Phase 7 第三个切片（2026-08-22）

- `ChatBridge` 保留房间、私信、消息和成员模型的 JSON 快照。
- 只有收到对应模型变更信号时才重新序列化该模型；100ms 状态发布节流保持不变。
- 重复切换当前会话时不重复绑定同一个模型，减少信号连接和重复序列化。
- 对外状态 JSON 结构、字段和 WebEngine/QWebChannel 接口保持兼容。

## Phase 7 采样与生命周期测试（2026-08-22）

- `ChatBridge` 记录最近一次状态构建耗时、脏模型数量和累计构建次数，指标只用于调试/测试，不驱动界面刷新。
- 500 条房间数据采样验证：首次构建重建 4 个模型，单个房间变化只重建 1 个模型。
- 增加 worker 断开收尾测试，验证 queued 调用在 worker 线程执行并能在限定时间内完成。
- 当前采样示例：单模型更新状态构建约 5.7ms；该数值随机器、构建模式和数据规模变化，不能作为固定性能承诺。

## Phase 8 首个切片（2026-08-22）

- 新增 `PerformanceSampler`，维护有界帧间隔窗口并计算 FPS、P95、P99、Max。
- 新增 `PerformanceOverlay.qml`，仅在 `LAN_CHAT_ENABLE_PERF_OVERLAY=ON` 的测试/诊断构建中加入 QML 资源并加载。
- 默认/发布构建不编译 sampler，也不打包 overlay，保持发布包的 UI 和线程开销不变。
- 文档补充 `QSG_INFO`、`QT_LOGGING_RULES`、`QSG_VISUALIZE` 和 QML Profiler 启动入口。
- 采样指标定位为 GUI 帧回调节奏；GPU 渲染耗时必须结合 Qt Scene Graph 日志和 Profiler 分析。

### Phase 8 收尾判定（2026-08-22）

- `PerformanceSampler` 和 `PerformanceOverlay.qml` 仅在 `LAN_CHAT_ENABLE_PERF_OVERLAY=ON` 时进入 `lan-chat-gui` 目标；默认/发布 GUI 不链接采样器，也不加载面板资源。
- `performance-sampler-tests` 覆盖 P95/P99/Max、FPS 和 240 条有界样本窗口。
- Debug WebEngine 构建的 GUI 测试与启动烟测通过；性能面板仍明确属于测试/诊断构建，不进入发布包。

## Phase 9 首个切片（2026-08-22）

- 新增 `PerformanceProfile`，提供 `Automatic`、`High`、`Balanced`、`Power Saving` 四种等级，并使用 `QSettings` 持久化用户选择。
- Automatic 依据软件渲染、硬件加速状态、屏幕刷新率和观测到的 P95 帧时间决定生效等级，不根据 GPU 型号或厂商硬编码。
- Settings 页面提供性能等级选择和当前生效等级；主窗口与聊天页只按策略关闭装饰性渐变/光晕，弹窗动画按策略缩放，聊天和网络业务保持不变。
- 公共按钮、输入框、滚动条、房间/私信项、消息和表情控件的颜色/透明度/缩放 Behavior 同样受动画策略控制；省电模式不再为这些非必要反馈创建动画。
- `PerformanceProfile` 在所有构建中以低开销方式监听 `QQuickWindow::frameSwapped`，每 4 帧汇总一次实际帧间隔；`PerformanceSampler` 和右上角面板仍严格属于诊断构建。
- Automatic 模式对帧耗时候选进行 3 次连续确认后才切换，避免短时尖峰导致等级抖动；软件渲染、无硬件加速和 60 Hz 以下刷新率仍立即执行硬限制。
- 设置页显示观测 FPS、P95/最大帧耗时和自动策略原因；诊断面板同时显示当前生效等级，便于真实硬件校准阈值。
- P9 后续仍需在真实硬件上采样并校准等级阈值；本切片不引入新的 Blur、ShaderEffect 或每条消息阴影。
