# Cross-Language Chat Protocol

本文档描述 Go Server 与 C++ Client 之间的当前协议。协议只处理文本聊天，不处理文件、图片、命令执行或二进制业务数据。

## 1. Transport

- Transport：TCP。
- Server listen address：0.0.0.0:8888。
- Client 连接时使用 Server 的 IPv4 地址和 TCP port。
- TCP 是字节流，不保证一次 send 对应一次 recv，所以协议必须自行定义消息边界。

## 2. Frame Format

每条消息编码为：

~~~text
+----------------------+---------------------------+
| 4-byte length        | JSON payload              |
| unsigned, big-endian | UTF-8 bytes              |
+----------------------+---------------------------+
~~~

length 是 payload 的字节数：

~~~text
1 <= length <= 65536 (64 KiB)
~~~

它不是 Unicode 字符数量。例如，中文字符在 UTF-8 中通常占多个字节，长度头必须使用 payload.size() 或 JSON 字节长度。

### Go 编码

- 写入：encoding/binary.BigEndian.PutUint32。
- 读取：encoding/binary.BigEndian.Uint32。
- io.ReadFull 确保 header 和 payload 读取完整。

### C++ 编码

- 写入：htonl() 后调用 send_all()。
- 读取：调用 recv_all() 后使用 ntohl()。
- send_all() 和 recv_all() 循环处理 partial send/recv。

## 3. Send/Receive Rules

发送步骤：

1. 将 Message 序列化为 UTF-8 JSON 字节串；
2. 计算 payload byte length；
3. 检查是否为空或超过 64 KiB；
4. 写入 4-byte big-endian header；
5. 写入完整 payload。

接收步骤：

1. 读取完整 4 字节 header；
2. 解析 length；
3. 拒绝 length == 0 或 length > 64 KiB；
4. 按 length 读取完整 payload；
5. 校验 UTF-8；
6. 解析 JSON，并检查字段类型和必需字段。

## 4. Message Schema

所有消息必须有字符串字段：

~~~json
{
  "type": "chat"
}
~~~

可选字段：

| Field | JSON type | 用途 |
|---|---|---|
| type | string | 消息类型，必需 |
| username | string | 显示名称 |
| user_code | string | 用户唯一代码 |
| target_user_code | string | `private_chat` 的目标用户代码 |
| room | string | 当前或目标房间名称 |
| content | string | 文本内容 |
| users | array of string | 在线用户身份列表 |
| rooms | array of string | 房间列表 |

当前身份显示格式为：

~~~text
username#user_code
~~~

user_code 由用户自定义，只允许 ASCII letters/digits，长度 3–16；服务端按不区分大小写的规则保证同一 Server 进程内唯一，并保留用户输入的显示大小写。用户名本身可以重复。

## 5. Message Types

### login

Client → Server：

~~~json
{
  "type": "login",
  "username": "Alice",
  "user_code": "ALICE001"
}
~~~

成功：

~~~json
{
  "type": "login_ok",
  "username": "Alice",
  "user_code": "ALICE001",
  "content": "Login successful"
}
~~~

失败：

~~~json
{
  "type": "login_error",
  "content": "User code already exists"
}
~~~

连接建立后必须先发送 login。非法登录连接会尽力收到错误消息，然后关闭。

### chat

Client → Server：

~~~json
{
  "type": "chat",
  "content": "你好，这是 UTF-8 消息。"
}
~~~

Server → all clients：

~~~json
{
  "type": "chat",
  "username": "Alice",
  "user_code": "ALICE001",
  "content": "你好，这是 UTF-8 消息。"
}
~~~

Server 根据当前 TCP connection 绑定身份，客户端提交的 username 和 user_code 不会被信任。

### private_chat

客户端输入 `/msg Name#Code message` 后发送：

~~~json
{
  "type": "private_chat",
  "target_user_code": "BOB01",
  "content": "你好，这是私聊消息。"
}
~~~

`Name` 只用于客户端命令和显示，服务端只使用 `target_user_code` 查找目标。`target_user_code` 必须是 3 到 16 位 ASCII 字母或数字；服务端比较代码时不区分大小写，因此 `BOB01` 和 `bob01` 指向同一个在线用户。发送者的 `username` 和 `user_code` 始终由服务端根据当前 TCP connection 填充，客户端不能通过请求字段伪造发送者身份。

服务端成功路由后，会把同一条消息分别投递给发送者和目标用户：

~~~json
{
  "type": "private_chat",
  "username": "Alice",
  "user_code": "ALICE001",
  "target_user_code": "BOB01",
  "content": "你好，这是私聊消息。"
}
~~~

私聊不会发送给群聊中的第三个用户。C++ 客户端通常显示为：

~~~text
[Private -> Bob#BOB01] 你好，这是私聊消息。
[Private from Alice#ALICE001] 你好，这是私聊消息。
~~~

以下情况只向发送者返回 `error`，不会产生私聊广播：

- 目标代码不存在：`Target user not found`
- 目标代码属于发送者自己：`Cannot send private message to yourself`
- 目标代码格式错误：`Invalid target user code`
- 私聊内容为空、非法 UTF-8 或超过 64 KiB：`Invalid private chat content`

### room_join / room_leave / rooms_request / rooms_response

客户端命令与协议对应关系：

```text
/join study_room  -> {"type":"room_join","room":"study_room"}
/leave            -> {"type":"room_leave"}
/rooms            -> {"type":"rooms_request"}
```

房间名只允许 1 到 32 个 ASCII 字母、数字或下划线。不存在的房间会在首次加入时创建；客户端初始位于 `lobby`。服务端返回：

```json
{
  "type": "rooms_response",
  "room": "study_room",
  "rooms": ["lobby", "study_room"]
}
```

群聊和上线/离线提示只发送给同一房间的客户端；私聊不受房间限制。在线用户列表使用 `Name#Code@Room` 表示所在房间。

### system

Server → clients：

~~~json
{
  "type": "system",
  "content": "Alice#ALICE001 joined the chat"
}
~~~

客户端离开时发送对应的 left the chat 提示。

### users_request / users_response

Client 输入 /users 后发送：

~~~json
{
  "type": "users_request"
}
~~~

Server 只返回给请求者：

~~~json
{
  "type": "users_response",
  "users": [
    "Alice#ALICE001",
    "Bob#BOB001"
  ]
}
~~~

列表由 Server 排序，Client 负责显示编号。

### quit

Client 输入 /quit 后发送：

~~~json
{
  "type": "quit"
}
~~~

Server 注销客户端、关闭连接，并向其他在线客户端广播离线提示。

### error

协议或业务输入不符合当前状态时，Server 可能发送：

~~~json
{
  "type": "error",
  "content": "Invalid chat content"
}
~~~

对于无法可靠解析的 frame，Server 不承诺发送错误消息，而是终止当前连接。

## 6. Invalid Frame and Disconnect Behavior

以下情况属于当前连接的协议错误：

- header 少于 4 bytes；
- length == 0；
- length > 64 KiB；
- payload 少于 header 声明的长度；
- payload 不是有效 UTF-8；
- malformed JSON；
- 缺少 type 或字段类型错误；
- users 不是 string array。

Go Server 会结束当前 read path，注销该 client，关闭连接；其他 client 和 Server 进程继续运行。C++ Client 在登录成功后的接收线程中遇到 recv == 0、SOCKET_ERROR 或 frame/JSON 解析失败时统一显示 `Connection to server lost.`，设置 running = false，执行 shutdown，等待 receive thread join，再关闭 socket。登录阶段若收不到 login response，则显示对应的登录接收错误并退出。

当前版本不自动重连。用户需要重新启动 Client 建立新 TCP connection。

## 7. Security Boundary

content 和其他字段只作为文本数据处理。Server 和 Client 不执行远程消息中的 cmd、PowerShell、shell 或任何系统命令，不使用 system() 处理网络输入。
