# LAN Chat Flutter UI 原型设计

**日期：** 2026-08-26
**状态：** 已确认，等待实现计划

## 目标

在不重写现有 Go 服务端、TLS 协议、房主启动和 C++ 网络逻辑的前提下，新增一个独立的 Flutter Windows UI 原型。原型必须连接现有服务端，实现实时收发消息、显示连接状态和断线提示。

## 范围

- 新 UI 位于仓库根目录 `flutter_client/`，与现有 `client-cpp/gui`、`frontend` 隔离。
- 新增 C++ Core 动态库，使用稳定 C ABI 向 Dart FFI 提供能力。
- 原型只实现一个聊天窗口：连接状态、会话列表、消息时间线、输入发送与断线提示。
- 继续保留 QML 和 React/WebEngine UI；Flutter 原型不替换现有默认启动器、安装器或发布包。

## 非目标

- 不用 Dart 重写 TLS、协议、局域网发现、房主启动、证书或数据库逻辑。
- 不删除、移动或修改 QML/React 的已有界面。
- 不在原型阶段迁移表情、消息搜索、成员资料弹层、历史分页、文件传输或性能等级设置。
- 不读取、复制或输出 `%LocalAppData%\DUSK1NG\LAN Chat\host` 内数据内容。

## 架构

```text
Flutter Windows UI
  -> dart:ffi
  -> lan_chat_core.dll (C ABI)
  -> existing C++ protocol / TLS / connection worker / local-host bootstrap
  -> existing Go TLS/TCP Hub
```

`lan_chat_core.dll` 是 Flutter 和现有 C++ 实现之间唯一的新边界。它负责把现有异步连接能力投影为 C ABI：创建/销毁客户端、连接、断开、发送消息、读取状态快照和读取事件。Flutter 不直接调用 Qt/QML 对象，C++ 网络线程也绝不直接触碰 Flutter UI 线程。

## 事件与状态模型

- C++ Core 维护最多 256 条事件的有界队列；事件类型至少包括连接状态变化、聊天消息、会话列表刷新和错误。队列满时丢弃最旧的可重建列表刷新事件，并发出一次溢出错误事件。
- Dart 通过周期性拉取或显式 wake-up 读取队列；每个事件使用 UTF-8 JSON 负载，跨 FFI 边界后立即复制并由原分配方释放。
- Core 的公共句柄不暴露 QObject、`std::string`、Qt 容器或线程对象。
- 关闭 Flutter 窗口时先取消 Dart 事件订阅，再调用 Core 的有界异步断开和销毁；UI 线程最多等待 5 秒，超时后记录错误并结束 Core 句柄。

## Flutter 原型界面

- 单窗口三栏布局：窄侧栏显示会话，主区显示选中会话消息，底部显示输入框和发送按钮。
- 顶栏始终显示 `未连接`、`连接中`、`已连接` 或 `正在重连`；错误显示为非阻塞提示。
- 首版视觉使用 Flutter Material 3 的少量主题 token，不引入复杂阴影、实时模糊或自定义着色器。
- 首版以 Windows 桌面为目标；窗口缩放、键盘 Enter 发送、Shift+Enter 换行和高 DPI 显示为人工验收项。

## 接口草案

```c
typedef void* LanChatCoreHandle;

LanChatCoreHandle lan_chat_core_create(void);
void lan_chat_core_destroy(LanChatCoreHandle handle);
int lan_chat_core_connect(LanChatCoreHandle handle, const char* request_json);
int lan_chat_core_disconnect(LanChatCoreHandle handle);
int lan_chat_core_send_message(LanChatCoreHandle handle, const char* request_json);
char* lan_chat_core_take_event_json(LanChatCoreHandle handle);
void lan_chat_core_free_string(char* value);
```

所有请求 JSON 都应在 C++ 中验证字段和长度。错误以事件或显式错误代码传回；不得把异常跨 C ABI 传播。

## 迁移顺序

1. 建立 Flutter Windows 工程、最小窗口和测试目录。
2. 提取不依赖 QML 的 C++ Core 接缝，先用单元测试锁定 C ABI 生命周期与事件队列。
3. 用 Dart FFI 封装 Core 句柄、字符串释放和事件流。
4. 实现连接状态、会话列表、消息时间线和发送输入框。
5. 接入现有 Go 服务进行双客户端收发与断线验证。
6. 仅在原型验收后，再设计发布打包、默认启动器切换或全量功能迁移。

## 验收标准

- Flutter Windows 原型能构建并启动，不依赖已运行的 QML/React UI。
- 原型能使用现有服务端完成连接，两个客户端之间能实时收发消息。
- 连接失败、断开和重连状态清晰可见，且不会令 Flutter 窗口无响应。
- 关闭窗口后 C++ Core 资源、网络线程和本地 Host 子进程能在 5 秒内释放。
- 现有前端、Go、Qt/C++ 测试继续通过，QML/React 可独立运行。
- 不生成或打包房主私钥、证书、数据库、聊天记录或日志。

## 风险与回滚

| 风险 | 控制 | 回滚 |
| --- | --- | --- |
| C++ Core 与 Qt GUI 耦合过深 | 先以最小 C ABI 和现有测试提取接缝 | 保持 QML/React 入口不变，停止 Flutter 分支 |
| FFI 内存所有权错误 | 所有跨边界字符串由 `lan_chat_core_free_string` 统一释放 | 禁用事件读取，保留网络 Core |
| Flutter 事件轮询造成空转 | 使用有界频率和批量事件读取 | 降低轮询频率或改为 Windows wake-up |
| 原型破坏现有客户端 | Flutter 独立目录、独立目标和独立启动命令 | 删除 Flutter 原型目标，不影响现有发布路径 |
