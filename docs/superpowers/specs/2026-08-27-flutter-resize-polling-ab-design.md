# Flutter 缩放轮询 A/B 诊断设计

## 背景

Windows 记事本和资源管理器缩放流畅，而 LAN Chat 的连接选择页与聊天页在 Release 模式下均卡顿。消息列表等页面专属布局因此不是共同根因。Flutter 客户端共同路径中，`ChatSessionController` 每 100ms 在 UI isolate 上跨 FFI 调用原生 Core 排空事件，是当前最可疑因素。

## 诊断方案

为 `LanChatFlutterApp` 增加只读构造参数 `startPolling`，默认值保持 `true`。程序入口通过编译期 `LAN_CHAT_DISABLE_POLLING` Dart define 决定是否关闭轮询：未设置时行为与当前正式程序完全相同；仅诊断构建设置为 `true`。

生成两个独立 Release 运行目录：

- A：正常 100ms Core 轮询。
- B：暂停 Core 轮询，仅用于窗口缩放对比，不用于聊天功能验收。

两版使用完全相同的 UI、Core DLL、Qt 和 OpenSSL 运行库。用户分别连续拖动窗口边缘，比较缩放体验。

## 测试

- Widget 测试证明默认构造仍启用 Core 轮询。
- Widget 测试证明关闭轮询的诊断构建不会定时调用 `drainEvents()`。
- 现有 Flutter、Core 测试和静态分析继续通过。
- 两个 Release 版本均能启动，8 个目标运行库均从各自目录加载。

## 判定

- B 明显比 A 流畅：确认 UI isolate 的周期性 FFI 轮询是主要根因，下一阶段设计事件驱动或后台轮询替代方案。
- A、B 同样卡顿：排除轮询假设，删除诊断开关，下一阶段用 Flutter `FrameTiming` 探针区分 Build 与 Raster 卡顿。

## 约束

- 不修改协议、原生 Core 行为或旧客户端。
- 不读取、复制、输出或散列证书、私钥、数据库、聊天记录或日志内容。
- B 版暂停接收 Core 事件，只允许用于缩放诊断，不作为正式程序交付。
