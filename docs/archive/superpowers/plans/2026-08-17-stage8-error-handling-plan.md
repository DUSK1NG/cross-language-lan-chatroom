# Stage 8：异常处理与稳定性实现计划

> 本计划基于 Stage 8 异常处理设计文档，只处理异常、断线、协议边界和资源清理，不增加新的聊天室业务功能。

## 总体目标

完成 Go 服务端和 C++ 客户端的异常路径加固，并用自动化测试和 localhost 集成测试证明：非法数据、异常断线和服务端关闭不会导致进程崩溃、goroutine 泄漏、线程未 join 或 socket 未清理。

## 实施约束

- 每个任务完成后独立编译或测试；
- 先写失败测试，再进行最小实现修改；
- 保持 Stage 7 已确认的用户身份规则不变：用户名可以重复，user_code 大小写不敏感且由服务端保证进程内唯一；
- 保持 Client.Send 只能由 Hub 写入和关闭；
- 不使用 system()、shell、远程命令执行或自动重连；
- 不引入新的第三方依赖；
- 所有正式代码使用 UTF-8；
- 所有 Go 修改使用 gofmt；
- 所有 C++ 修改使用 C++17 和警告选项验证。

## Task 1：补全 Go 帧协议异常测试

目标：用确定性测试覆盖长度头、payload 截断和最大消息长度边界。

修改范围：

- server-go/protocol_test.go；
- 如测试暴露协议实现缺陷，再最小修改 server-go/protocol.go。

实现步骤：

1. 添加不足 4 字节长度头测试；
2. 添加长度为 0 测试；
3. 添加超过 64 KiB 测试；
4. 添加声明长度大于实际 payload 的截断测试；
5. 添加完整 back-to-back frame 测试，确认异常测试不会破坏原有分帧行为；
6. 使用明确的错误断言，避免只检查进程没有崩溃。

验证：

~~~powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
Set-Location server-go
go test ./... -run 'Frame|ReadFrame|WriteFrame'
~~~

## Task 2：补全 Go JSON、UTF-8 和字段类型异常测试

目标：证明完整 payload 进入 JSON 解析前已经经过 UTF-8 和消息结构检查。

修改范围：

- server-go/message_test.go；
- 必要时最小修改 server-go/message.go。

实现步骤：

1. 添加非法 UTF-8 payload 测试；
2. 添加 malformed JSON 测试；
3. 添加缺少 type 测试；
4. 添加 users 非数组测试；
5. 添加 users 数组包含非字符串测试；
6. 检查未知消息类型和错误字段类型不会产生可用的半成品 Message；
7. 保留中文 chat 消息的 UTF-8 round-trip 测试。

验证：

~~~powershell
Set-Location server-go
go test ./... -run 'Message|UTF8|Malformed|WrongField|Users'
~~~

## Task 3：补全 Go 连接生命周期和异常断线测试

目标：证明非法连接和异常断线只影响当前客户端，Hub 和 Server 仍可继续工作。

修改范围：

- server-go/client_test.go；
- server-go/hub_test.go；
- 必要时最小修改 server-go/client.go。

实现步骤：

1. 使用 net.Pipe 覆盖登录前 EOF 和登录后 EOF；
2. 覆盖连接重置或读错误后的 read goroutine 退出；
3. 覆盖发送非法帧、非法 JSON 后连接终止；
4. 验证异常客户端注销后，另一个客户端仍能登录和接收消息；
5. 验证注销请求重复到达时不会重复关闭 channel 或连接；
6. 验证登录错误路径不会阻塞等待不存在的 writer；
7. 如果发现错误回复与关闭顺序有竞态，只通过既有 Hub outbound 所有权模型修复。

验证：

~~~powershell
Set-Location server-go
go test ./... -run 'HandleConnection|Disconnect|Unregister|Quit|Error'
go test -race ./... -run 'HandleConnection|Disconnect|Unregister|Quit|Error'
~~~

## Task 4：补全 Hub 并发安全和写失败回归测试

目标：针对 Stage 7 曾发现的 send-on-closed-channel 风险，证明 Hub 是 Send channel 的唯一写入和关闭者。

修改范围：

- server-go/hub_test.go；
- server-go/client_test.go；
- 只有测试暴露真实竞态时才修改 server-go/hub.go 或 server-go/client.go。

实现步骤：

1. 测试已注销客户端收到晚到 outbound 事件时被忽略；
2. 测试写端失败后注销和资源清理；
3. 测试满发送队列不会让 Hub panic；
4. 测试异常客户端离开后正常客户端仍能收到系统消息和 chat；
5. 使用 go test -race 验证 map、channel 和客户端状态没有数据竞争；
6. 不通过增加全局 mutex 绕过 Hub 所有权设计。

验证：

~~~powershell
Set-Location server-go
go test -race ./...
go vet ./...
~~~

## Task 5：审查并加固 C++ 错误返回和清理路径

目标：保证 Winsock 连接失败、发送失败、接收 EOF、接收错误和非法协议帧都能汇聚到安全退出路径。

修改范围：

- client-cpp/src/protocol.cpp；
- client-cpp/src/message.cpp；
- client-cpp/src/main.cpp；
- 必要时对应 client-cpp/include/*.hpp。

实现步骤：

1. 检查 send_all 和 recv_all 对 0 字节、部分传输和 SOCKET_ERROR 的处理；
2. 检查长度为 0、超过 64 KiB 时是否在分配 payload 前返回失败；
3. 检查 JSON 类型错误、非法 users 数组和缺少字段时是否返回失败；
4. 检查接收线程失败后是否设置 running = false 并唤醒主线程；
5. 检查主线程退出时是否只执行一次 shutdown/close，并 join 接收线程；
6. 保证错误信息不会把远程内容当作命令执行；
7. 只有发现实际缺陷才修改代码，纯审查结果记录在测试报告中。

验证：

~~~powershell
Set-Location client-cpp
g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp src\message.cpp src\protocol.cpp -Iinclude -Ithird_party -o chat-client.exe -municode -lws2_32
~~~

## Task 6：执行 localhost 异常场景集成测试

目标：在真实 TCP socket 上验证服务端和客户端的异常退出行为。

范围：

- 使用现有 Go Server 和 C++ Client 可执行文件；
- 允许使用临时测试辅助程序构造非法帧，但不能修改正式协议；
- 测试结束清理进程、临时可执行文件、日志和缓存。

测试步骤：

1. 启动 Go Server，确认监听 0.0.0.0:8888；
2. 启动 C++ Client，测试不存在端口的连接失败；
3. 启动一个客户端并强制结束，确认 Server 继续运行；
4. 启动两个客户端，关闭其中一个，确认另一个仍能聊天；
5. 突然停止 Server，确认 Client 显示连接丢失并退出；
6. 发送超长或非法测试帧，确认 Server 拒绝连接且进程继续运行；
7. 检查新客户端仍可重新连接；
8. 检查没有残留客户端、服务端或 8888 监听。

验收输出：

- 记录每个异常场景的实际结果；
- 记录 Go 测试、race、vet 和 C++ 编译结果；
- 记录未通过项及其原因，不能用“没有崩溃”代替协议结果断言。

## Task 7：更新文档并执行全量验证

目标：让 GitHub 项目能够展示 Stage 8 的稳定性设计和可复现验证方式。

修改范围：

- README.md；
- docs/protocol.md（如果当前已存在或 Stage 8 需要补充）；
- 必要时增加 docs/testing.md，不复制大段代码。

文档内容：

- 最大 payload 和非法帧处理；
- 客户端强制退出和服务端关闭时的行为；
- Windows 编译和 localhost 异常测试命令；
- 当前限制：无自动重连、无 TLS、无持久化；
- 局域网测试前仍需先通过 localhost 测试。

全量验证：

~~~powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
Set-Location server-go
gofmt -w *.go
go test ./...
go test -race ./...
go vet ./...
go build -o chat-server.exe .
Set-Location ..\client-cpp
g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp src\message.cpp src\protocol.cpp -Iinclude -Ithird_party -o chat-client.exe -municode -lws2_32
~~~

最后执行 git diff --check、git status --short 和进程/端口检查，确认工作区干净且无残留资源。

## 推荐提交顺序

1. test: cover malformed go frames
2. test: cover invalid go messages
3. test: cover go connection cleanup
4. test: cover hub write failure safety
5. fix: harden cpp socket cleanup（仅在确有代码修改时提交）
6. test: verify stage8 localhost failures
7. docs: document stage8 reliability checks

每次提交前都运行与本任务相关的最小测试；最终合并前再执行全量验证和独立代码审查。
