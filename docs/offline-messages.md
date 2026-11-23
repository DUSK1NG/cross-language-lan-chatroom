# 离线消息

当前版本的离线消息仅针对已注册账号的私聊。

## 行为

1. Alice 使用 `/msg Bob#BOB001 你好`。
2. 如果 Bob 在线，消息立即发送。
3. 如果 Bob 不在线，但 `BOB001` 是已注册代码，消息保存到服务端 `chat.db`。
4. Bob 下次登录成功后，服务端推送：

```json
{
  "type": "offline_message",
  "username": "Alice",
  "user_code": "A001",
  "target_user_code": "BOB001",
  "content": "你好"
}
```

5. 消息推送成功后从数据库删除，不会重复发送。

## 存储限制

- 数据表：`offline_messages`
- 每个用户最多保留 100 条离线私聊消息；超出时删除最旧消息。
- 消息内容仍受协议最大长度限制。
- 群聊消息不会保存为离线消息。
- 临时访客代码不是持久账号，因此不提供跨断线离线消息。

## 服务器模式兼容性

局域网版将离线私聊写入 Rust 服务端的 `chat.db`。两种启动方式都支持该功能。

- 手工运行 `lan-chat-server`。
- 桌面客户端启动房主子进程。

客户端收到离线消息时显示为：

```text
[Offline private from Alice#A001] 你好
```
