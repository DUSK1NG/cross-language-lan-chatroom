# Stage 7：聊天室命令与持续交互设计

## 1. 目标

在现有 Go + C++ 跨语言局域网聊天室基础上，实现三个第一版命令：

- `/users`：查看当前在线用户；
- `/help`：查看客户端帮助；
- `/quit`：主动退出聊天室。

本阶段同时把 C++ 客户端从“一次登录、一次发言、收到消息后退出”的冒烟程序改为持续交互式命令行客户端。

本阶段不实现私聊、聊天室、数据库、GUI、TLS、文件传输或公网通信。

## 2. 已确认的实现方案

采用事件驱动 Hub 方案：

- Go Hub goroutine 是 `Clients`、`ActiveCodes`、`UsedCodes` 的唯一读写者；
- 连接 goroutine 不直接读取或修改 Hub map；
- `users_request` 通过 Hub channel 传入，由 Hub 生成在线列表并定向回复；
- `quit` 由连接 goroutine 请求 Hub 注销；
- `/help` 只在 C++ 客户端本地处理，不产生网络请求。

这样可以延续 Stage 6 的 channel 并发模型，不额外引入 mutex 保护在线用户 map。

## 3. 协议设计

所有消息继续使用 4 字节大端长度头 + UTF-8 JSON payload。

### 3.1 Message 结构

Go `Message` 增加：

```go
Users []string `json:"users,omitempty"`
```

C++ `Message` 增加：

```cpp
std::vector<std::string> users;
```

### 3.2 查看在线用户

客户端发送：

```json
{
  "type": "users_request"
}
```

服务端在 Hub goroutine 内读取当前已登录客户端，生成显示身份并按字典序排序：

```json
{
  "type": "users_response",
  "users": [
    "Alex#A001",
    "Alex#B002"
  ]
}
```

用户列表使用 `username#user_code`，以便区分显示名相同但身份代码不同的用户。没有在线用户时返回空数组。

### 3.3 退出

客户端发送：

```json
{
  "type": "quit"
}
```

服务端不要求额外响应，立即通过 Hub 注销客户端，保留 `UsedCodes` 中的代码，并向其他在线客户端广播离线系统消息。

### 3.4 帮助

`/help` 不发送网络消息，由客户端显示：

```text
/users  - Show online users
/help   - Show this help message
/quit   - Leave the chat
```

未知的 `/` 开头命令显示本地错误，不发送为聊天消息。普通文本仍发送 `chat` 消息。

## 4. Go 服务端设计

`message.go` 的消息校验新增：

- `users_request`：不要求 `content`；
- `quit`：不要求 `content`；
- `users_response`：允许 `users` 数组，服务端生成，不接受客户端伪造。

`Hub` 增加请求 channel，例如：

```go
RequestUsers chan *Client
```

Hub 收到请求后：

1. 遍历 `Clients`；
2. 将每个客户端转换为 `username#user_code`；
3. 排序结果；
4. 只向请求客户端的 `Send` channel 写入 `users_response`。

`Client.readPump` 使用消息类型分支：

- `chat`：校验内容，覆盖服务端绑定身份后广播；
- `users_request`：发送 `RequestUsers`；
- `quit`：发送 `Hub.Unregister` 并返回；
- 其他类型：发送 `error`。

注销流程保持幂等：重复注销不会重复关闭 channel 或重复广播离线消息。

## 5. C++ 客户端设计

登录握手由主线程完成，收到 `login_ok` 后启动接收线程。

### 主线程

- Windows 控制台读取 UTF-16 输入，通过 `WideCharToMultiByte` 转为 UTF-8；
- `/help`：本地打印帮助；
- `/users`：发送 `users_request`；
- `/quit`：发送 `quit`，设置 `running=false`，调用 `shutdown(socket, SD_BOTH)`；
- 普通文本：发送 `chat`。

### 接收线程

接收线程只负责读取和显示：

- `chat`：显示 `username#user_code: content`；
- `system`：显示上线或离线消息；
- `users_response`：显示编号列表；
- `error`：显示服务端错误；
- 接收失败或 `recv == 0`：显示 `Connection to server lost.`，设置 `running=false` 并调用 `shutdown`。

只由主线程在接收线程结束后执行 `join`、`closesocket` 和 `WSACleanup`，避免两个线程重复关闭 Socket。

输出使用互斥锁，避免接收线程和主线程同时写控制台造成输出交错。

## 6. 错误处理与安全边界

- 未登录连接不能进入命令处理；
- 无效消息返回 `error`；
- `users_request` 不带内容；
- `quit` 不等待额外响应；
- 用户列表为空时返回空数组；
- 所有远程内容只当作文本处理；
- 不调用 `system()`，不执行 cmd、PowerShell、Shell 或任何远程命令；
- 不新增数据库、TLS、文件执行或任意代码执行能力。

## 7. 测试与验收

### Go 测试

- `users_request` 返回排序后的 `users_response`；
- 用户列表包含 `username#user_code`；
- 多用户请求不产生数据竞态；
- `quit` 注销当前客户端并向剩余客户端广播离线消息；
- 重复退出和异常断线不会重复广播；
- `go test ./...`；
- `go test -race ./...`；
- `go vet ./...`。

### C++ 与集成测试

- C++17 + MinGW-w64 编译通过且无新增警告；
- 两个相同用户名、不同代码同时在线；
- `/users` 显示完整在线身份；
- `/help` 显示命令说明；
- `/quit` 正常退出且服务器继续运行；
- 服务端突然关闭时客户端安全退出；
- 中文聊天消息正常显示；
- 先 localhost 双客户端验证，再进行局域网验证。

## 8. 完成标准

Stage 7 完成时必须满足：

1. `/users`、`/help`、`/quit` 均可实际使用；
2. 在线列表由 Go Hub 安全生成，客户端不能伪造；
3. C++ 客户端可以持续收发聊天消息；
4. 线程、Socket 和 channel 能安全退出；
5. Go 测试、竞态检查、静态检查和 C++ 编译全部通过；
6. localhost 集成测试通过，且没有残留测试服务进程。
