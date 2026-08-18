# Cross-Language Chat Protocol

## 账号认证协议

Go Server 使用 `database/sql`、SQLite 和 bcrypt 保存账号。数据库路径由 `-db` 或 `CHAT_DB_PATH` 指定，默认是 `chat.db`。

### register

客户端发送：

```json
{
  "type": "register",
  "username": "Alice",
  "user_code": "ALICE01",
  "password": "correct-password"
}
```

成功响应：

```json
{
  "type": "register_ok",
  "content": "Registration successful"
}
```

用户名和用户代码均不区分大小写且必须唯一。重复用户名或代码返回 `register_error`。

### login_auth

注册成功后，客户端发送：

```json
{
  "type": "login_auth",
  "username": "Alice",
  "password": "correct-password"
}
```

密码通过 bcrypt 校验。成功后服务端返回原有的 `login_ok`，并从数据库绑定保存的用户名和用户代码：

```json
{
  "type": "login_ok",
  "username": "Alice",
  "user_code": "ALICE01",
  "content": "Login successful"
}
```

密码错误或账号不存在返回 `login_error`。服务端只保存 bcrypt 哈希，不保存密码明文。旧的 `login` 消息仍兼容未注册身份；如果身份已经存在于账号数据库，旧登录会返回 `Password login required`。

本文档描述 Go Server 与 C++ Client 之间的当前协议。协议只处理文本聊天，不处理文件、图片、命令执行或二进制业务数据。

## 1. Transport

- Transport：TCP。
- Server listen address：0.0.0.0:8888。
- Server transport security：TLS 1.2 or newer，证书由服务端启动参数或环境变量提供。
- Client 连接时使用 Server 的 IPv4 地址和 TCP port。
- TCP 是字节流，不保证一次 send 对应一次 recv，所以协议必须自行定义消息边界。

TLS 只负责加密和认证 TCP 字节流，不改变上层 4-byte length header、UTF-8 JSON 和消息类型。当前 Go 服务端已启用 TLS；C++ 客户端仍需后续增加 TLS 客户端握手后才能连接。

服务端启动配置：

```powershell
.\chat-server.exe -cert .\certs\server.crt -key .\certs\server.key
```

C++ 客户端已经使用 OpenSSL 3.x 完成 TLS 握手；自签名开发证书需要在客户端通过 `--ca-file` 显式信任。

也可以设置：`CHAT_TLS_CERT` 和 `CHAT_TLS_KEY`。证书或私钥缺失时服务端会明确报错并退出，不会回退到明文 TCP。

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

Go Server 会结束当前 read path，注销该 client，关闭连接；其他 client 和 Server 进程继续运行。C++ Client 在登录成功后的接收线程中遇到 recv == 0、SOCKET_ERROR 或 frame/JSON 解析失败时统一显示 `Connection to server lost.`，关闭当前连接并进入 `Reconnecting...` 循环；连接成功后重新发送 `login`，等待 `login_ok`，再继续接收。用户主动退出、输入结束或本地输入错误时，客户端会禁止重连，等待 receive thread join 后再关闭 socket。登录阶段若收到明确的 `login_error`，则显示登录错误并退出。

C++ Client 在 TCP 连接异常断开后会自动建立新 TCP connection，并重新发送原始 `login` 消息。重连等待为 1、2、4、8、16、30 秒，之后保持 30 秒上限；只有收到 `login_ok` 后才恢复发送聊天、私聊和房间消息。`/quit`、输入结束和本地输入错误不会触发重连。

## 7. Security Boundary

content 和其他字段只作为文本数据处理。Server 和 Client 不执行远程消息中的 cmd、PowerShell、shell 或任何系统命令，不使用 system() 处理网络输入。
## TLS 传输层

C++ 客户端在长度头和 JSON 协议外层使用 OpenSSL 3.x TLS。TLS 只负责加密和认证，4-byte big-endian length header + JSON payload 不变。

- 默认使用系统信任库并启用 SSL_VERIFY_PEER。
- 客户端同时校验服务器证书链和服务器 IP/主机名。
- 自签名开发证书必须通过 --ca-file path 显式指定信任 CA。
- 不允许通过命令行关闭证书验证。
- 自动重连会重新创建 SSL_CTX 和 SSL，重新执行证书验证与 SSL_connect。

当前 Go Server 已提供 TLS listener；客户端和服务端证书配置一致后即可进行端到端 TLS 联调。
## 8. 账号认证

账号模式必须使用 TLS。密码字段只用于 `register` 和 `login_auth`，客户端不会把密码写入日志。

注册请求：

```json
{
  "type": "register",
  "username": "Alice",
  "user_code": "ALICE001",
  "password": "password123"
}
```

注册成功返回 `register_ok`，失败返回 `register_error`。注册成功后，客户端继续发送：

```json
{
  "type": "login_auth",
  "username": "Alice",
  "password": "password123"
}
```

未提供 `--password` 时，客户端发送旧版 `login`，用于兼容无账号模式。自动重连只重新发送 `login_auth` 或兼容的 `login`，不会重复发送 `register`。
