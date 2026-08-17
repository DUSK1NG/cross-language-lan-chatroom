# Go + C++ 跨语言局域网聊天室总体设计

日期：2026-08-17  
状态：已获用户确认，等待文档审阅  
项目：Cross-Language LAN Chatroom

## 1. 项目目标

开发一个适合本科生完成和展示的局域网多人聊天室：

- 服务端使用 Go；
- 客户端使用 C++17 和 Windows Winsock2；
- 通信使用 TCP/IPv4；
- 支持多个客户端同时在线；
- 支持 UTF-8 中文消息；
- 支持 Ethernet 服务端和 Wi-Fi 客户端互通；
- 不依赖公网服务器；
- 第一版使用命令行，不实现 GUI、账号密码、文件传输或 TLS。

第一版重点学习 TCP 字节流、Socket、网络字节序、消息分帧、JSON、goroutine、channel 和 C++ 线程。

## 2. 总体架构

```text
C++ Client A ─┐
C++ Client B ─┼── TCP/IPv4 ── Go Server 0.0.0.0:8888
C++ Client C ─┘                    │
                                   └── Hub goroutine
                                       ├── Client Reader goroutines
                                       └── Client Writer goroutines
```

服务端监听 `0.0.0.0:8888`，使同一台电脑的 localhost、Ethernet 网卡和 Wi-Fi 网卡都可以被使用。客户端连接服务器真实的局域网 IPv4，例如 `192.168.1.100:8888`，不能把 `0.0.0.0` 当作目标地址。

### 2.1 服务端组件

- Accept Loop：调用 `net.Listen` 和 `Accept` 接收连接；
- Hub：独占在线客户端 map，处理注册、注销、用户名检查和广播；
- Client Reader：从一个 TCP 连接读取完整协议消息；
- Client Writer：从该客户端的 `Send` channel 取消息并写入 Socket；
- Protocol：实现 4 字节长度头、UTF-8 payload 和 JSON 编解码。

### 2.2 客户端组件

- Main Thread：`std::getline` 读取输入、解释 `/users`、`/help`、`/quit` 并发送消息；
- Receive Thread：后台读取完整协议消息、解析 JSON 并打印；
- Network：Winsock 初始化、连接、`send_all`、`recv_all` 和关闭；
- Protocol：长度头和 JSON 序列化、反序列化。

## 3. Go 并发模型

最终使用一个 Hub goroutine、每个客户端一个 Reader goroutine、每个客户端一个 Writer goroutine。

Hub 独占在线客户端 map。客户端 Reader 不直接修改共享 map，而是通过 channel 向 Hub 发送事件。Hub 按顺序处理注册、注销和广播，因此避免多个 goroutine 并发读写普通 map，也避免用户名检查和注册之间产生竞态。

一个客户端在登录成功之前不加入在线列表。登录请求发送给 Hub 后，由 Hub 检查用户名是否为空、是否重复、是否超过限制，然后完成注册并发送 `login_ok`。登录失败时发送 `login_error`，连接可以保留以允许用户重试。

每个客户端只能有一个 Writer goroutine 写 Socket，避免多个 goroutine 同时写同一个 TCP 连接造成消息交错。`Send` channel 设置有限容量；如果客户端长期无法接收，第一版可以将其视为异常客户端并断开，不能让一个慢客户端阻塞整个 Hub。

## 4. TCP 应用层协议

每条应用层消息使用以下格式：

```text
┌────────────────────┬────────────────────────┐
│ 4 Byte Length      │ JSON Payload           │
│ uint32，大端序      │ UTF-8 字节             │
└────────────────────┴────────────────────────┘
```

长度头表示 JSON payload 的字节数，不表示字符数，也不包括长度头本身。最大 payload 为 64 KiB；长度为 0 或超过上限时拒绝该数据。

发送流程：

1. 将消息序列化为 JSON；
2. 得到 UTF-8 字节序列；
3. 计算 payload 字节长度；
4. Go 使用 `binary.BigEndian`，C++ 使用 `htonl` 生成 4 字节头；
5. 使用 `send_all` 发送完整长度头；
6. 使用 `send_all` 发送完整 payload。

接收流程：

1. 使用 `io.ReadFull` 或 `recv_all` 读取 4 字节；
2. Go 使用 `binary.BigEndian`，C++ 使用 `ntohl` 解析长度；
3. 检查长度是否在 `1..64 KiB`；
4. 循环读取 N 个 payload 字节；
5. 解析完整 JSON；
6. 根据 `type` 执行业务处理。

TCP 是有序、可靠的字节流，不保留应用层消息边界。因此一次 `send` 不一定对应一次 `recv`，必须实现 `send_all`、`recv_all` 和长度分帧。

## 5. JSON 消息协议

消息类型固定为：

| 类型 | 方向 | 用途 |
|---|---|---|
| `login` | 客户端 → 服务端 | 用户登录 |
| `login_ok` | 服务端 → 客户端 | 登录成功 |
| `login_error` | 服务端 → 客户端 | 登录失败 |
| `chat` | 双向 | 聊天消息 |
| `system` | 服务端 → 客户端 | 上线、离线提示 |
| `users_request` | 客户端 → 服务端 | 请求在线列表 |
| `users_response` | 服务端 → 客户端 | 返回在线列表 |
| `quit` | 客户端 → 服务端 | 主动退出 |
| `error` | 服务端 → 客户端 | 协议或业务错误 |

登录请求：

```json
{"type":"login","username":"Alice"}
```

登录成功：

```json
{"type":"login_ok","content":"Login successful"}
```

登录失败：

```json
{"type":"login_error","content":"Username already exists"}
```

客户端聊天请求：

```json
{"type":"chat","content":"大家好"}
```

服务器广播时根据 TCP 连接绑定的用户名生成 `username`，不信任客户端自行发送的用户名：

```json
{"type":"chat","username":"Alice","content":"大家好","time":"14:30:20"}
```

上线和离线消息：

```json
{"type":"system","content":"Alice joined the chat"}
```

```json
{"type":"system","content":"Alice left the chat"}
```

在线用户请求和响应：

```json
{"type":"users_request"}
```

```json
{"type":"users_response","users":["Alice","Bob","Charlie"]}
```

所有 JSON 字符串使用 UTF-8。客户端使用 `std::string` 保存 UTF-8 字节，不能把中文字符数当作协议长度。

## 6. 客户端命令

- `/help`：客户端本地显示帮助，不发送到服务器；
- `/users`：发送 `users_request`；
- `/quit`：发送 `quit`，然后安全关闭；
- 其他不以 `/` 开头的输入：发送 `chat`。

远程消息始终作为文本处理。项目禁止使用 `system()`，禁止执行 cmd、PowerShell、shell、文件或任意远程代码。

## 7. 状态和异常处理

客户端连接状态：

```text
Disconnected → Connected → WaitingLogin → Online → Closing → Disconnected
```

服务端需要处理：

- `io.EOF`；
- TCP connection reset；
- broken pipe；
- 客户端崩溃或强制关闭；
- 非法 JSON；
- 未知消息类型；
- 空用户名；
- 重复用户名；
- 超长长度头；
- 未登录客户端发送业务消息。

客户端需要处理：

- `connect` 失败；
- `send` 失败；
- `recv == 0`；
- `SOCKET_ERROR`；
- 服务器突然关闭；
- Socket 关闭后的线程退出。

服务器发现客户端离线后必须：

```text
删除客户端
    ↓
关闭连接
    ↓
广播离线消息
```

C++ 客户端使用 `std::atomic<bool> running` 控制状态。主线程负责最终 `closesocket` 和 `WSACleanup`，接收线程退出前必须被 `join`。用户输入 `/quit` 时使用 `shutdown` 唤醒接收线程，避免 `recv` 永久阻塞。

## 8. 项目目录

```text
cross_language_lan_chat/
│
├── server-go/
│   ├── main.go
│   ├── hub.go
│   ├── client.go
│   ├── protocol.go
│   └── go.mod
│
├── client-cpp/
│   ├── src/
│   │   ├── main.cpp
│   │   ├── network.cpp
│   │   └── protocol.cpp
│   │
│   ├── include/
│   │   ├── network.hpp
│   │   └── protocol.hpp
│   │
│   ├── third_party/
│   │   └── json.hpp
│   │
│   └── CMakeLists.txt
│
├── docs/
│   └── protocol.md
│
├── screenshots/
└── README.md
```

Go 使用 `net`、`encoding/json`、`encoding/binary`、`io` 和必要的并发原语。C++ 使用 C++17、Winsock2、`std::thread`、`std::atomic`、STL 和单头文件 `nlohmann/json`。

## 9. 开发阶段

### Stage 1：最简单 TCP 双向通信

只实现 Go 服务端和 C++ 客户端的单客户端文本通信：客户端发送 `Hello`，服务器返回 `Received`。先在 localhost 验证。

验收：连接成功、消息成功往返、服务器和客户端都能正常退出。

### Stage 2：加入 4 字节长度头

暂时仍使用普通文本 payload，重点验证大端序、`send_all`、`recv_all` 和 TCP 分帧。

验收：连续快速发送多条消息时，每条消息仍能独立恢复，没有粘包和拆包错误。

### Stage 3：加入 JSON、登录和聊天

加入 `nlohmann/json`、Go `encoding/json`、`login`、`login_ok`、`login_error` 和 `chat`。

验收：一个 C++ 客户端可以登录，发送中文，服务器返回正确 JSON。

### Stage 4：多客户端和 goroutine

允许多个客户端同时连接，每个连接使用独立 goroutine。作为并发学习过渡，可以使用受保护的共享结构，但必须清楚记录锁的范围。

验收：三个客户端可以同时连接，服务器不会因为一个客户端断开而退出。

### Stage 5：Hub 和广播

将客户端注册、注销和广播集中到 Hub goroutine，通过 channel 传递事件。

验收：任意客户端发送聊天消息，所有在线客户端都能收到；Hub map 不存在并发读写。

### Stage 6：用户名系统

实现登录、空用户名检查、重复用户名检查、服务器绑定用户名、上线和离线系统消息。

验收：重名登录失败；客户端断开后被移除；其他客户端收到离线提示。

### Stage 7：命令

实现 `/users`、`/help`、`/quit`。

验收：用户列表准确；帮助不污染群聊；退出后线程和 Socket 正常释放。

### Stage 8：异常和边界测试

测试非法 JSON、未知类型、超长消息、错误长度、客户端强制关闭和服务器突然关闭。

验收：错误客户端被清理，Go 服务端不崩溃，C++ 客户端显示 `Connection to server lost.` 并安全结束。

### Stage 9：局域网测试

先用同一台电脑的多个客户端，然后使用 Ethernet 服务端和 Wi-Fi 客户端。

验收：客户端能够连接服务器实际局域网 IPv4，三台设备可以互相群聊。

### Stage 10：项目展示

完善 README、协议文档、架构图、构建说明、局域网说明、截图和限制说明。

验收：其他同学按照 README 可以独立编译、启动和测试项目。

## 10. Windows 和局域网要求

服务端运行在 Windows 时，使用 `ipconfig` 查找 Ethernet 网卡的 IPv4 地址。客户端连接服务器真实地址，例如 `192.168.1.100:8888`。

不要关闭整个 Windows Defender Firewall。必要时只允许 Go Server 通过 Private Network，或创建只允许 TCP 8888 的入站规则。

测试 TCP 连通性优先使用：

```powershell
Test-NetConnection 192.168.1.100 -Port 8888
```

`ping` 不通不一定表示 TCP 不通，因为 ICMP 可能被防火墙禁止。还要检查 AP Isolation、Client Isolation、Guest Wi-Fi、VLAN 和 Windows 网络配置。

## 11. 测试案例

至少覆盖：

1. Go Server + 一个 C++ Client；
2. Go Server + 三个 C++ Client；
3. 中文消息：`你好，这是 Go 和 C++ 跨语言聊天室。`；
4. 快速发送 100 条消息，检查完整性、顺序和分帧；
5. 客户端强制关闭；
6. 重复用户名；
7. `/users`；
8. 服务器突然关闭；
9. Wi-Fi 客户端连接 Ethernet 服务端。

## 12. 简历成果

项目完成后可以描述为：

> 独立开发基于 Go 与 C++ 的跨语言局域网多人聊天室，使用 TCP Socket 实现客户端/服务器通信，设计 4 字节长度头 + JSON 的应用层协议解决 TCP 粘包与拆包问题；Go 服务端采用 goroutine/channel 管理并发客户端及消息广播，C++ 客户端基于 Winsock2 与 std::thread 实现异步收发，并完成 Wi-Fi 与以太网跨设备局域网通信测试。

## 13. 设计自检结论

- 服务端监听地址、客户端目标地址和局域网测试方式一致；
- Go 和 C++ 的长度头均为 4 字节、`uint32`、大端序；
- 长度定义明确为 UTF-8 payload 字节数；
- `send_all`、`recv_all` 已纳入协议边界；
- 服务器绑定连接用户名，不信任聊天消息中的用户名；
- Hub 独占共享客户端 map；
- 每个客户端只有一个 Writer 写 Socket；
- 异常断线、非法 JSON、非法长度和超长消息都有处理策略；
- C++ Socket 类型、Winsock 错误值和关闭方式符合 Windows；
- 分阶段顺序从 localhost 开始，逐步扩展到局域网；
- 第一版明确不实现账号、文件、GUI、TLS、P2P 和远程命令执行。

