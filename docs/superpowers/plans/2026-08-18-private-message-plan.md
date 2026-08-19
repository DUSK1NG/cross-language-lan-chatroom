# 私聊功能实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` for implementation task execution when the user selects that execution mode.

**Goal:** 为现有 Go + C++ 局域网聊天室增加基于唯一用户代码的在线私聊，并保证私聊不泄露给其他群聊成员。

**Architecture:** 客户端发送 `private_chat` 请求和目标代码；Go `Hub` 通过新增私聊事件查找规范化代码对应的在线连接，构造绑定发送者身份的消息，分别投递到发送者和目标的写队列。C++ 客户端增加 `/msg Bob#BOB01 内容` 解析和方向化显示。

**Tech Stack:** Go、`encoding/json`、goroutine/channel、C++17、Winsock2、nlohmann/json、现有长度头协议、Go tests、CMake/CTest。

**Spec:** `docs/superpowers/specs/2026-08-18-private-message-design.md`

## 全局约束

- 所有用户代码比较不区分大小写，服务端的规范化结果是权威值。
- 客户端提交的 `username`、`user_code` 不能决定发送者身份。
- 私聊必须绕过群聊 `Broadcast`，只投递给两方。
- TCP 帧仍使用 4 字节大端长度头，JSON payload 长度按 UTF-8 字节计算。
- 不使用 `system()`、shell、远程命令执行或任何文件执行逻辑。
- 每个任务完成后运行对应测试；最终必须执行 Go 测试、C++ 构建/CTest，并进行 localhost 集成验证。

---

## 任务 1：扩展跨语言消息模型和校验

**文件：**

- 修改 `server-go/message.go`
- 修改 `client-cpp/include/message.hpp`
- 修改 `client-cpp/src/message.cpp`
- 修改 `server-go/message_test.go`（如现有测试文件命名不同，以实际文件为准）
- 修改 `client-cpp/tests/protocol_tests.cpp`

**实施内容：**

1. Go `Message` 增加 `TargetUserCode string \`json:"target_user_code,omitempty"\``。
2. C++ `Message` 增加同名字段，并保持旧的聚合初始化调用可编译。
3. Go `validateMessage` 增加 `private_chat`：目标代码必须非空且符合现有代码规则，正文必须是非空 UTF-8 文本并不超过 64 KiB。
4. C++ JSON 序列化只在字段非空时写入 `target_user_code`；反序列化时要求该字段为字符串。
5. 增加有效私聊、缺失目标、空正文、非法代码、超长正文和 UTF-8 的测试。

**验收：** Go 与 C++ 对同一私聊 JSON 使用完全相同的字段名；现有登录、群聊、在线列表和协议测试不回归。

## 任务 2：实现 Go Hub 私聊路由

**文件：**

- 修改 `server-go/hub.go`
- 修改 `server-go/client.go`
- 修改 `server-go/hub_test.go` 或现有 Hub 测试文件

**实施内容：**

1. 新增 `PrivateMessageRequest` 和 Hub 私聊请求 channel。
2. 在 `Hub.Run` 中处理私聊事件，使用 `ActiveCodes` 查找目标，不让其他 goroutine直接访问 map。
3. 目标不存在、自己发送给自己时，只向发送者投递 `error`。
4. 成功时由 Hub 构造 `private_chat`，填入发送者连接绑定的 `Username/UserCode`、目标规范化前的在线代码和正文。
5. 将消息分别放入发送者与目标的 `Send` channel，不调用群聊广播。
6. `readPump` 增加 `private_chat` 分支，只提取目标代码和正文并提交 Hub；忽略或覆盖客户端伪造的身份字段。
7. 复用现有注销、慢客户端和连接异常处理，确保私聊错误不会使 Hub 崩溃。

**验收：** 两个客户端可互相私聊；第三个客户端收不到；代码大小写混合仍能找到目标；目标下线后发送方收到明确错误。

## 任务 3：实现 C++ `/msg` 命令和私聊显示

**文件：**

- 新增 `client-cpp/include/command.hpp`
- 新增 `client-cpp/src/command.cpp`
- 修改 `client-cpp/src/main.cpp`
- 修改 `client-cpp/CMakeLists.txt`
- 新增 `client-cpp/tests/command_tests.cpp`

**实施内容：**

1. 提取可测试的私聊命令解析函数，解析 `/msg Bob#BOB01 你好`。
2. 拒绝缺少目标、缺少 `#`、空代码、空正文和仅有空格的正文。
3. 返回目标代码和正文，目标显示名只用于本地命令提示，不作为服务端身份依据。
4. 主线程发送 `Message{type="private_chat", target_user_code=..., content=...}`。
5. 接收线程增加 `private_chat` 显示逻辑，使用当前登录身份区分 `Private from` 与 `Private ->`。
6. `/help` 增加 `/msg Name#Code message` 示例。
7. CMake 将命令解析源文件加入生产客户端，并增加命令解析测试目标和 CTest 注册。

**验收：** 中文私聊正文不乱码；接收线程不会阻塞输入；客户端断线仍能安全 join；非法命令只显示本地帮助，不发送错误帧。

## 任务 4：补充路由和跨语言回归测试

**文件：**

- 修改 `server-go/hub_test.go` 或现有测试文件
- 修改 `server-go/client_test.go` 或现有测试文件
- 修改 `client-cpp/tests/protocol_tests.cpp`
- 修改 `client-cpp/tests/command_tests.cpp`

**实施内容：**

1. 构造三个已登录测试客户端，验证私聊只到达发送者和目标。
2. 验证未知代码、自发消息和大小写不敏感代码。
3. 验证客户端提交伪造 `username/user_code` 时，服务端转发仍使用 TCP 连接绑定身份。
4. 验证目标断线、发送队列关闭、错误 JSON 和超长帧不会导致 Hub panic。
5. 验证 C++ 私聊 JSON round-trip、中文内容和命令解析边界。
6. 保持现有 13 个协议测试和所有 Go race 测试通过。

**验收：** `go test ./...`、`go test -race ./...`、`go vet ./...`、C++ 构建和 CTest 全部通过；测试能明确证明第三方客户端不可见。

## 任务 5：更新协议文档和展示材料

**文件：**

- 修改 `docs/protocol.md`
- 修改 `README.md`
- 修改 `docs/testing.md`

**实施内容：**

1. 增加 `private_chat` 的请求与服务端转发 JSON 示例。
2. 说明 `target_user_code` 的大小写不敏感规则、在线限制和错误行为。
3. 增加 `/msg` 使用说明和私聊输出示例。
4. 增加 localhost 三客户端验收步骤：A 发给 B，C 确认看不到。
5. 在 README 功能列表和限制/后续升级部分准确描述当前私聊能力。
6. 保持 Windows、Wi-Fi + Ethernet、防火墙和安全边界说明不被削弱。

**验收：** 新用户仅阅读 README 与协议文档即可构造合法私聊帧并完成三客户端测试；文档中的字段与代码一致。

## 最终验证顺序

1. `gofmt` 检查并格式化 Go 文件。
2. `go test ./...`、`go test -race ./...`、`go vet ./...`。
3. MinGW 或 MSVC 构建 C++ 客户端。
4. CTest 执行协议和命令测试。
5. localhost 启动 Go 服务端，连接三个 C++ 客户端，验证群聊、私聊、未知目标和 `/users`。
6. 检查 `git diff`，确认没有生成物、密钥或本机路径进入提交。

