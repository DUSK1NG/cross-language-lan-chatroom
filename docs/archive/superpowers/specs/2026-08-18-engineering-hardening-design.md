# 工程化完善设计：CMake、跨平台 CI 与协议测试

## 背景

当前聊天室已经完成核心功能、协议异常处理和局域网验证，但 C++ 客户端只有直接的 MinGW 编译命令，GitHub 也没有自动验证流程。后续改动如果缺少统一构建入口和自动测试，容易出现“本机可用、仓库不可复现”的问题。

## 目标

本子项目只完善工程化基础，不增加聊天业务功能：

1. 为 Windows C++ 客户端提供 CMake 构建入口，同时保留现有 MinGW 和 MSVC 命令；
2. 增加 C++ 协议层自动化测试，覆盖长度头、半包、非法 JSON、字段类型和 UTF-8；
3. 在 GitHub Actions 中使用 Windows latest 和 Ubuntu latest 验证项目；
4. 更新 README，使构建入口、平台边界和 GitHub 状态准确可复现。

## 非目标

本轮不实现自动重连、TLS、数据库、GUI、私聊、聊天室、文件传输，也不强行把 Windows 专用 Winsock 客户端移植到 Ubuntu。

## 构建设计

`client-cpp/CMakeLists.txt` 使用 C++17，创建以下目标：

- `chat-client`：由 `src/main.cpp`、`src/message.cpp`、`src/protocol.cpp` 组成，链接 `ws2_32`；
- `protocol-tests`：由协议测试源文件和 `src/message.cpp`、`src/protocol.cpp` 组成，链接 `ws2_32`；
- `enable_testing()` 与 `add_test(NAME protocol-tests COMMAND protocol-tests)`：交给 CTest 执行。

测试目标直接复用生产协议接口：`protocol::send_all`、`protocol::recv_all`、`protocol::send_frame`、`protocol::recv_frame`、`message::send_message`、`message::receive_message`。测试通过本机 TCP loopback 建立成对连接，避免依赖 Unix-only `socketpair`。

## C++ 测试设计

测试程序使用 Windows loopback listener 和一个辅助线程，为每个用例提供连接；每个失败断言返回非零退出码。覆盖：

- `send_frame` 拒绝空 payload 和超过 `protocol::kMaxMessageSize` 的 payload；
- `recv_frame` 拒绝长度为 0、超过 64 KiB、长度头不足 4 字节和 payload 截断；
- `message::receive_message` 拒绝 malformed JSON、缺少字符串 `type`、字段类型错误；
- 合法中文 `chat` 消息完成 UTF-8 字节往返；
- 多次发送的 frame 按发送顺序读取，确认协议边界不依赖单次 `recv`。

完整交互式客户端“用户输入后自然退出”不纳入本轮自动化；该场景继续保留 Windows Terminal 手工验收。

## CI 设计

`.github/workflows/ci.yml` 使用 push 和 pull request 触发两个 job：

### Windows job

- 安装/使用 Go；
- 执行 `go test ./...`、`go test -race ./...`、`go vet ./...`；
- 执行 `go build -o chat-server.exe .`；
- 使用 `cmake -S client-cpp -B client-cpp/build -G "MinGW Makefiles"` 配置；
- 使用 `cmake --build client-cpp/build --config Release` 构建；
- 使用 `ctest --test-dir client-cpp/build --output-on-failure` 执行 C++ 测试。

### Ubuntu job

- 执行 Go 测试、race、vet 和 build；
- 不构建 C++ 客户端，因为生产客户端依赖 Winsock2 和 Windows Unicode 控制台。

## 文档设计

README 增加：

- 当前版本 `v1.0.0`；
- GitHub 仓库链接；
- CMake 构建和 CTest 命令；
- MinGW/MSVC/CMake 的平台说明；
- CI 检查内容；
- 已完成的局域网测试和当前限制。

删除或修正与当前事实不符的“尚未配置 origin”描述。真实截图仍由开发者在测试环境中补充，不在自动化流程中生成。

## 验收标准

完成后必须满足：

1. Windows 上 MinGW 直接编译仍成功；
2. Windows 上 CMake configure、build、CTest 全部成功；
3. Go 的 test、race、vet、build 全部成功；
4. Ubuntu job 能完成 Go 验证；
5. C++ 测试失败时 CTest 返回非零，不能吞掉错误；
6. README 中的命令、仓库地址和平台限制与实际一致。
