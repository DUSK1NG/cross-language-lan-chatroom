# Stage 5：Hub Goroutine 与群聊广播实施计划

## 本阶段目标

将 Stage 4 中由 `sync.Mutex` 保护的客户端注册表重构为 Hub 模型，并完成真正的群聊广播：

- 一个 Hub goroutine 独占客户端集合；
- 每个已登录客户端拥有一个写入 goroutine；
- 每个客户端连接由一个读取流程接收消息；
- 客户端发送聊天消息后，服务器将消息广播给所有已登录客户端，包括发送者；
- 客户端断开后，Hub 删除客户端并关闭其发送 channel；
- 不因单个客户端断线、发送失败或异常消息导致服务器崩溃。

## 本阶段明确不做

- `/users`；
- `/help`、`/quit`；
- 上线/离线系统消息；
- 私聊、房间、数据库、TLS、GUI；
- 重复用户名策略的新增功能（沿用现有登录校验，后续阶段再扩展）。

## 设计要点

### Hub 的职责

`Hub` 持有 `map[*Client]bool`，并通过三个 channel 接收事件：

- `Register`：加入一个已完成登录的客户端；
- `Unregister`：移除一个断开的客户端；
- `Broadcast`：接收一条需要广播的消息。

只有 `Hub.Run` goroutine 读写客户端 map，因此不需要让多个业务 goroutine 直接修改共享 map，也避免遗漏锁或锁粒度不一致造成的数据竞争。

### Client 的职责

每个客户端包含：

- TCP 连接；
- 服务器确认后的用户名；
- 带缓冲的 `Send chan Message`；
- 用于保证连接只关闭一次的 `sync.Once`。

登录握手仍由连接处理流程完成。登录成功后启动写入 goroutine，之后：

- 读取流程只负责从 TCP 连接读取完整 JSON 消息并提交给 Hub；
- 写入 goroutine 只负责从 `Send` channel 读取消息并写入 TCP 连接；
- Hub 只负责客户端集合和消息分发。

这样可以避免多个 goroutine 同时对同一个 TCP 连接执行写操作。

### 广播规则

Stage 5 中，Hub 将聊天消息发送给所有在线客户端，包括消息发送者。每个客户端的 `Send` channel 使用有限缓冲，避免 Hub 因普通短暂写入延迟而立即阻塞。实际发送失败由该客户端的写入流程处理，并触发连接清理。

### 连接关闭规则

只有 Hub 关闭客户端的 `Send` channel；连接关闭使用客户端内部的 `sync.Once` 保证重复清理安全。读取失败、写入失败和服务器主动移除都可以触发清理，但不会因为重复关闭 TCP 连接而影响其他客户端。

## 文件修改

### 新增

- `server-go/hub.go`：定义 `Client`、`Hub`、Hub 事件循环和广播逻辑；
- `server-go/hub_test.go`：测试注册、广播到全部客户端以及注销关闭 channel。

### 修改

- `server-go/main.go`：启动 Hub goroutine；每个 TCP 连接交给独立连接处理流程；
- `server-go/client.go`：拆分登录、读取流程、写入流程和安全关闭逻辑；将聊天消息提交给 Hub，不再直接回写单个客户端。

### 删除

- `server-go/registry.go`；
- `server-go/registry_test.go`。

Stage 4 的 mutex registry 将由 Hub 完全替代，避免两套客户端管理机制并存。

## 测试策略

先用 Go 单元测试确定性验证 Hub 行为，再进行跨语言联调：

1. 测试两个虚拟客户端注册后，单条聊天消息能分别从两个 `Send` channel 取到；
2. 测试注销客户端后，其 `Send` channel 被关闭；
3. `go test ./...`；
4. `go test -race ./...`，确认 Hub、读取流程和写入流程没有数据竞争；
5. `go vet ./...`；
6. 编译 C++ 客户端；
7. 启动 Go 服务端，运行三个 C++ 客户端，确认多个客户端都能收到聊天广播；
8. 强制结束一个客户端，确认服务器仍在运行，并且其他客户端仍可继续通信。

## 阶段验收标准

- Hub goroutine 是客户端 map 的唯一读写者；
- 每个已登录客户端最多只有一个写入 goroutine；
- 一条聊天消息可送达所有已注册客户端；
- 客户端断开不会让 Go 服务端退出；
- Go 测试、竞态检测、静态检查均通过；
- Windows 下 C++ 客户端可以继续连接并完成中文聊天广播；
- 能清楚解释 `Register`、`Unregister`、`Broadcast` 三个 channel 的作用。

## 执行顺序

1. 先添加 Hub 单元测试，明确广播和注销行为；
2. 实现 Hub；
3. 重构客户端读写流程和主程序；
4. 删除旧 registry；
5. 格式化、测试、编译和局域网前的 localhost 联调；
6. 提交 Stage 5 变更并输出验收结果。
