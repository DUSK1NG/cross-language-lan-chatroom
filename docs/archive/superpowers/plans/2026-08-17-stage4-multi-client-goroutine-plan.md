# Stage 4：多客户端与 goroutine 实现计划

> **执行说明：** 本阶段只增加 Go 服务端的多连接接收和 goroutine 并发处理。每个客户端仍然执行一次登录和一次聊天回显，Hub、广播、在线列表和重复用户名检查留到后续阶段。

**目标：** 让 Go 服务端能够同时接受多个 C++ 客户端，每个 TCP 连接由独立 goroutine 处理，互不阻塞。

**架构：** `main` goroutine 持续执行 `Accept` 循环；每次接收连接后立即启动 `go handleConnection(conn, registry)`。Stage 4 临时使用带 `sync.Mutex` 的 `ClientRegistry` 记录在线连接和用户名，用于学习共享 map 的线程安全；Stage 5 再把它替换为 Hub goroutine。

## 全局约束

- 服务端必须持续 Accept，不能处理完一个客户端就退出。
- 每个连接使用独立 goroutine；一个客户端的异常不能导致服务端退出。
- 每个客户端仍执行 Stage 3 流程：`login` → `login_ok` → `chat` → `chat` 回显。
- 暂不实现 Hub、广播、`/users`、重复用户名拒绝或聊天记录。
- 允许多个客户端暂时使用同名用户名；重复用户名检查放到后续阶段。
- 共享客户端 registry 的 map 必须由 `sync.Mutex` 保护。
- 客户端网络协议、JSON 字段和 UTF-8 规则保持不变。
- 所有客户端连接关闭后必须从 registry 移除。

---

### 任务 1：为 ClientRegistry 编写并实现线程安全测试

**文件：**

- 创建：`server-go/registry.go`
- 创建：`server-go/registry_test.go`

**接口：**

```go
type ClientRegistry struct { ... }

func NewClientRegistry() *ClientRegistry
func (r *ClientRegistry) Add(conn net.Conn, username string)
func (r *ClientRegistry) Remove(conn net.Conn)
func (r *ClientRegistry) Count() int
```

- [ ] **步骤 1：先编写失败测试**

测试：

1. 新 registry 的数量为 0；
2. `Add` 一个连接后数量为 1；
3. `Remove` 后数量回到 0；
4. 多个 goroutine 同时 `Add` 和 `Remove` 时不会 panic，最终数量正确。

测试可以使用 `net.Pipe()` 创建连接，并在测试结束时关闭两端。

- [ ] **步骤 2：运行测试确认先失败**

在 `server-go` 执行：

```powershell
$env:Path = "C:\Users\jking1\go-sdk\go\bin;$env:Path"
$env:GO111MODULE = "off"
$env:GOCACHE = "C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache"
go test ./...
```

预期：因为 `ClientRegistry` 尚未定义，编译失败。

- [ ] **步骤 3：实现互斥保护的 registry**

使用：

```go
type ClientRegistry struct {
	mu      sync.Mutex
	clients map[net.Conn]string
}
```

`Add`、`Remove` 和 `Count` 都必须在访问 map 前后正确加锁。`Remove` 对不存在的连接保持安全，不 panic。

- [ ] **步骤 4：运行测试和竞态检测**

```powershell
gofmt -w registry.go registry_test.go
go test ./...
go test -race ./...
```

预期：普通测试和竞态检测都通过。

---

### 任务 2：拆出单客户端处理器

**文件：**

- 创建：`server-go/client.go`
- 修改：`server-go/main.go`

**接口：**

```go
func handleConnection(conn net.Conn, registry *ClientRegistry)
```

- [ ] **步骤 1：移动 Stage 3 的登录和聊天流程**

将当前 `main.go` 中的单客户端登录、聊天和 JSON 响应流程移动到 `handleConnection`。

`handleConnection` 开始时：

```go
defer conn.Close()
```

连接登录成功后：

```go
registry.Add(conn, username)
defer registry.Remove(conn)
```

连接异常、客户端主动断开或聊天处理完成时，都必须执行清理。

- [ ] **步骤 2：保留服务器绑定用户名行为**

聊天响应继续使用登录阶段保存的 `username`，不能使用聊天 JSON 中的 `username` 字段。

- [ ] **步骤 3：为连接生命周期增加日志**

至少打印：

```text
Client connected: <remote address>
Login username: <username>
Active clients: <count>
Client disconnected: <remote address>
```

日志只用于观察并发流程，不能在多个 goroutine 中直接读写未保护的共享 map。

- [ ] **步骤 4：运行 Go 测试和构建**

```powershell
gofmt -w main.go client.go registry.go registry_test.go message.go message_test.go protocol.go protocol_test.go
go test ./...
go test -race ./...
go vet ./...
go build -o chat-server.exe .
```

---

### 任务 3：改造 Accept 循环支持多个客户端

**文件：**

- 修改：`server-go/main.go`

- [ ] **步骤 1：创建 registry 并持续 Accept**

服务器启动后创建一个 `ClientRegistry`，然后持续执行：

```go
for {
	conn, err := listener.Accept()
	if err != nil {
		log.Printf("failed to accept client: %v", err)
		continue
	}
	go handleConnection(conn, registry)
}
```

Accept 失败时记录日志并继续监听；不能因为一次 Accept 错误让整个服务端退出。

- [ ] **步骤 2：确保主 goroutine 不提前退出**

服务端不能在第一个客户端完成聊天后结束。只有进程被用户终止时才停止监听。

- [ ] **步骤 3：编译和静态检查**

```powershell
$env:Path = "C:\Users\jking1\go-sdk\go\bin;$env:Path"
$env:GO111MODULE = "off"
$env:GOCACHE = "C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache"
go test ./...
go test -race ./...
go vet ./...
go build -o chat-server.exe .
```

---

### 任务 4：多客户端 localhost 验收

**文件：**

- 不新增协议文件；使用现有 `client-cpp/chat-client.exe`。

- [ ] **步骤 1：启动持续运行的服务器**

启动：

```powershell
cd server-go
./chat-server.exe
```

预期：服务器保持监听，不因第一个客户端退出而结束。

- [ ] **步骤 2：启动三个 C++ 客户端**

分别使用不同参数：

```powershell
chat-client.exe 127.0.0.1 8888 Alice "Hello from Alice"
chat-client.exe 127.0.0.1 8888 Bob "Hello from Bob"
chat-client.exe 127.0.0.1 8888 Charlie "Hello from Charlie"
```

三个客户端可以先后或近似同时启动；每个客户端都必须成功登录并收到自己的聊天回显。

- [ ] **步骤 3：验证并发处理**

用三个独立进程同时启动客户端，确认：

- 三个连接都能成功建立；
- 三个客户端都能完成登录和聊天；
- 一个客户端退出不影响其他客户端；
- 服务端继续监听下一个连接；
- registry 数量在连接建立和关闭时正确变化。

- [ ] **步骤 4：提交 Stage 4**

所有 Go 测试、竞态检测、C++ 编译和三个客户端验收通过后执行：

```powershell
git add server-go docs/superpowers/plans/2026-08-17-stage4-multi-client-goroutine-plan.md
git commit -m "feat: add stage4 multi-client goroutine handling"
```

## Stage 4 验收清单

- [ ] 服务端持续 Accept；
- [ ] 每个客户端由独立 goroutine 处理；
- [ ] registry 的 map 使用 `sync.Mutex` 保护；
- [ ] `go test`、`go test -race`、`go vet` 通过；
- [ ] 三个 C++ 客户端可以同时完成登录和聊天回显；
- [ ] 一个客户端断开不会导致服务端退出；
- [ ] 暂未加入 Hub、广播、在线用户列表或重复用户名检查。

