# 跨语言通信协议

## 1. 传输层

- 传输协议：TCP
- 服务端监听：`0.0.0.0:8888`
- 安全层：TLS 1.2 或更高版本
- 字符编码：UTF-8
- 消息内容只作为文本数据处理，不执行命令或脚本

TLS 只负责加密 TCP 字节流，不改变应用层消息格式。

## 2. 消息帧

```text
+----------------------+---------------------------+
| 4-byte length        | UTF-8 JSON payload      |
| unsigned, big-endian |                           |
+----------------------+---------------------------+
```

长度字段表示 JSON Payload 的字节数：

```text
1 <= length <= 65536
```

它不是 Unicode 字符数量。Go 使用 `binary.BigEndian`，C++ 使用 `htonl()` 和 `ntohl()`。

TCP 不保证一次 `send()` 对应一次 `recv()`，因此双方必须循环读取完整的 4 字节长度头和完整 Payload。

## 3. 登录

客户端发送：

```json
{
  "type": "login",
  "username": "Bob",
  "user_code": "B001"
}
```

成功：

```json
{
  "type": "login_ok",
  "username": "Bob",
  "user_code": "B001",
  "content": "Login successful"
}
```

失败：

```json
{
  "type": "login_error",
  "content": "User code already exists"
}
```

用户名可以重复，用户代码由服务端按不区分大小写的规则保证唯一。

## 4. 群聊

客户端发送：

```json
{
  "type": "chat",
  "content": "你好，这是中文消息。",
  "room": "lobby"
}
```

服务端广播：

```json
{
  "type": "chat",
  "username": "Alice",
  "user_code": "A001",
  "room": "lobby",
  "content": "你好，这是中文消息。"
}
```

发送者身份由服务端根据当前 TCP 连接绑定，客户端不能伪造。

## 5. 私聊

```json
{
  "type": "private_chat",
  "target_user_code": "B001",
  "content": "你好"
}
```

私聊只发送给发送者和目标用户，不受频道限制。目标代码不存在、目标是自己或消息内容非法时，服务端只向发送者返回 `error`。

## 6. 频道、权限和在线成员

```json
{"type":"room_join","room":"study_group"}
{"type":"room_create","room":"private_group","private":true}
{"type":"room_leave"}
{"type":"rooms_request"}
{"type":"users_request"}
```

频道只能由 `room_create` 创建；`room_join` 只会加入已存在的频道。私有频道仅管理员、频道创建者和被邀请的成员可见、可加入。

频道创建者或全局管理员可发送：

```json
{"type":"room_action","content":"invite","room":"private_group","target_user_code":"B001"}
{"type":"room_action","content":"remove_member","room":"private_group","target_user_code":"B001"}
{"type":"room_action","content":"delete","room":"private_group"}
```

服务端返回：

```json
{
  "type": "rooms_response",
  "rooms": ["lobby", "study_group"],
  "room": "study_group",
  "room_details": [
    {"name":"lobby","private":false,"can_manage":false},
    {"name":"study_group","owner_code":"A001","private":true,"can_manage":true}
  ]
}
```

```json
{
  "type": "users_response",
  "users": ["Alice#A001@lobby", "Bob#B001@study_group"],
  "user_details": [
    {"username":"Alice","user_code":"A001","room":"lobby","is_admin":true}
  ]
}
```

Qt 客户端连接期间会定时请求成员和频道列表，实现自动刷新；收到非当前会话消息时，会在对应频道或私聊列表显示未读数量。

## 7. 系统提示

```json
{
  "type": "system",
  "room": "lobby",
  "content": "Bob#B001 joined the chat"
}
```

系统提示带有所属房间，客户端只将其显示在对应频道，不显示在私聊窗口。

## 8. 错误和断线

以下情况会拒绝当前消息或关闭当前连接：

- 长度头不足 4 字节
- 长度为 0 或超过 64 KiB
- Payload 不是合法 UTF-8
- JSON 格式错误
- 缺少必要字段
- 用户代码重复
- 客户端异常断开

单个客户端的异常不能使 Go Server 崩溃，也不能影响其他客户端。
