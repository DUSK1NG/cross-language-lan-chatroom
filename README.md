# Go + C++ LAN Chat

一个面向 Windows 11 的跨语言局域网多人聊天室：Go 负责 TCP Server，C++ 负责 Winsock2 Client。项目使用自定义应用层 framing 和 JSON 协议，适合学习 TCP 字节流、并发、跨语言协议和异常清理。

## 项目特点

- Go Server：`net`、`goroutine`、`channel`，监听 `0.0.0.0:8888`
- C++ Client：C++17、Winsock2、`std::thread`、`std::atomic`
- TCP 多客户端群聊，支持 Wi-Fi 与 Ethernet 在同一局域网内互通
- 中文消息统一使用 UTF-8
- 用户名可以重复；通过自定义 ASCII 字母/数字 `user_code` 区分身份
- 支持 `/help`、`/users`、`/msg Name#Code message`、`/quit`
- 支持按唯一 `user_code` 定向私聊；代码比较不区分大小写，用户名可以重复
- 支持长度头、半包、粘包、非法 JSON、非法 UTF-8、超长帧和异常断线处理
- C++ 客户端目前仍为 Windows 专用（Windows-only）

## 当前仓库状态

- 公开仓库：<https://github.com/DUSK1NG/cross-language-lan-chatroom>
- 当前仓库事实：`origin` 已配置为 `https://github.com/DUSK1NG/cross-language-lan-chatroom.git`
- 默认发布分支 / 公开仓库主线为 `master`
- 当前里程碑：`v1.0.0`

## Architecture

```text
C++ Client A ─┐
C++ Client B ─┼── TCP: 4-byte big-endian length + UTF-8 JSON ── Go Server
C++ Client C ─┘                                     0.0.0.0:8888
```

Go Server 使用一个 Hub goroutine 管理客户端 `map`、注册、注销、广播和定向响应。每个已登录客户端有读取路径和写入路径；`Client.Send` 只由 Hub 写入和关闭，避免多个 goroutine 同时操作共享 map 或触发 `send on closed channel`。

完整架构图见 [docs/architecture.md](docs/architecture.md)。

## 目录结构

```text
server-go/                 Go TCP Server
client-cpp/                C++17 Windows Client
docs/protocol.md           framing 和 JSON 协议
docs/testing.md            构建、自动化测试和局域网测试
docs/architecture.md       Mermaid 架构图
docs/github-publishing.md  GitHub 发布清单
screenshots/               局域网测试截图
docs/superpowers/          设计、实现计划和任务记录
```

## Protocol 简介

每条 TCP 应用层消息都是：

```text
4-byte unsigned length header, big-endian
        +
UTF-8 JSON payload
```

长度表示 JSON payload 的字节数，不是字符数。`payload` 必须满足 `1 <= length <= 64 KiB`。接收端先读取完整 4 字节 header，再读取完整 payload；因此 TCP 的粘包和拆包不会改变消息边界。

主要消息类型包括：`login`、`login_ok`、`login_error`、`chat`、`private_chat`、`system`、`users_request`、`users_response`、`quit`、`error`。完整字段和异常行为见 [docs/protocol.md](docs/protocol.md)。

## Windows 环境

推荐环境：

- Windows 11
- Go 1.20 或更高版本
- MinGW-w64 g++，支持 C++17
- Visual Studio Developer PowerShell（可选，用于 MSVC）
- Windows Terminal 或 PowerShell，并使用 UTF-8 code page
- C++ 依赖：`client-cpp/third_party/json.hpp`（`nlohmann/json` 单头文件）

## Build Server

PowerShell：

```powershell
cd server-go
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
go build -o chat-server.exe .
```

如果 Go 已经在系统 `PATH` 中，也可以直接运行：

```powershell
go build -o chat-server.exe .
```

## Build Client

MinGW 和 MSVC 直接构建命令在 `client-cpp` 目录执行。

MinGW 直接构建：

```powershell
g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp src\command.cpp src\message.cpp src\protocol.cpp -Iinclude -Ithird_party -o chat-client.exe -municode -lws2_32
```

MSVC 直接构建：

```powershell
cl /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE src\main.cpp src\command.cpp src\message.cpp src\protocol.cpp /Iinclude /Ithird_party ws2_32.lib /Fe:chat-client.exe
```

CMake 构建与测试从仓库根目录执行：

```powershell
cmake -S client-cpp -B client-cpp/build -G "MinGW Makefiles"
cmake --build client-cpp/build --config Release
ctest --test-dir client-cpp/build --output-on-failure
```

## CI / GitHub Actions

- Windows job 进行完整验证：Go `test` / `test -race` / `vet` / `build`，以及 C++ CMake configure、build、CTest
- Ubuntu job 只验证 Go Server，不编译 Windows C++ 客户端
- 远程 CI 结果应与本地自检一起作为发布前依据

## Run：localhost

先启动 Server：

```powershell
cd server-go
.\chat-server.exe
```

另开 PowerShell 启动 Client：

```powershell
cd client-cpp
.\chat-client.exe 127.0.0.1 8888 Alice ALICE001
```

启动第二个客户端时必须使用不同的 `user_code`，例如：

```powershell
.\chat-client.exe 127.0.0.1 8888 Bob BOB001
```

登录后可以输入：

```text
/help
/users
你好，这是 Go 和 C++ 的跨语言聊天。
/msg Bob#BOB001 你好，这是一条私聊消息。
/quit
```

房间功能：`/rooms` 查看房间，`/join room_name` 加入或创建房间，`/leave` 返回 `lobby`。群聊只发送给同一房间用户，私聊仍可跨房间发送；在线用户显示为 `Name#Code@Room`。

私聊命令格式为 `/msg Name#Code message`。其中 `Code` 是目标用户的唯一代码，服务端按代码查找目标并忽略客户端伪造的发送者身份。代码匹配不区分大小写；一条私聊只发送给发送者和目标用户，不会广播给其他客户端。目标不存在或向自己发送时，发送者会收到 Server error。

## Run：LAN

服务端必须监听所有网卡，而不是只监听 `127.0.0.1`。当前 Server 使用：

```text
0.0.0.0:8888
```

在服务端电脑执行 `ipconfig`，找到 Ethernet 或 Wi-Fi 网卡的 IPv4，例如 `192.168.1.100`。客户端使用该地址：

```powershell
.\chat-client.exe 192.168.1.100 8888 Alice ALICE001
```

Wi-Fi 和 Ethernet 可以互通，因为 TCP 建立在 IP 之上；只要两台设备路由可达即可。若连接失败，检查 Windows Private Network 防火墙、TCP 8888 入站规则、路由器 AP Isolation / Client Isolation、VLAN，以及设备是否在同一网段。不要为了测试而关闭整个 Windows Defender Firewall。

详细测试流程见 [docs/testing.md](docs/testing.md)。

实际 Stage 9 局域网验证已通过：Ethernet 服务端 `192.168.0.3` 与 Wi-Fi 客户端 `192.168.0.108` 成功建立 TCP 连接，Alice/Bob 完成多人中文聊天和断线清理测试。

## Screenshots

测试截图说明见 [screenshots/README.md](screenshots/README.md)，建议展示 Server 监听、多个客户端中文聊天、`/users` 和 Ethernet/Wi-Fi 拓扑。

## 稳定性与已覆盖行为

- 长度头不足 4 字节、payload 截断、长度为 0 或超过 64 KiB：拒绝当前 frame
- 非法 UTF-8、malformed JSON、缺少 `type`、字段类型错误：拒绝当前消息；协议错误会终止当前连接
- 客户端 `EOF`、`connection reset`、`recv == 0`、`SOCKET_ERROR`：清理当前连接，不影响 Server 和其他客户端
- Server 突然关闭，或 C++ Client 在登录成功后的接收线程中遇到 frame/JSON 接收解析失败：统一显示 `Connection to server lost.`
- Go Server 的 `Send` channel 由 Hub 统一写入和关闭；异常客户端不会拖垮整个 Server
- C++ 不执行远程消息中的命令，不使用 `system()`、shell 或远程代码执行

## C++ 客户端自动重连

服务器断开后，C++ 接收线程保持运行并自动重连。重连等待采用指数退避：1、2、4、8、16、30 秒，后续尝试保持 30 秒上限。重连成功后客户端会自动重新发送原始用户名和 `user_code` 的 `login` 消息，收到 `login_ok` 后恢复群聊、私聊和房间操作。

客户端会显示 `Connection to server lost.` 和 `Reconnecting...`。用户输入 `/quit`、输入结束或本地输入错误时会禁止重连，并安全等待接收线程退出。

## 当前限制

第一版明确不包含：

- TLS
- 账号密码和持久化数据库
- 聊天室房间、文件 / 图片 / 音视频传输
- GUI

当前已经有 C++ 协议层 loopback 自动化测试和 GitHub Actions，但交互式控制台“自然输入/退出”、真实局域网组网和完整发布验证仍需要在真实 Windows 环境中手工复核。不要把这些场景写成“已全部本地自动化通过”。

## 后续方向

聊天室房间、Qt GUI、SQLite 聊天记录、TLS，以及 Android / Web 客户端。

## Resume 项目描述

独立开发基于 Go 与 C++ 的跨语言局域网多人聊天室，使用 TCP Socket 和 4-byte big-endian length + UTF-8 JSON 应用层协议处理粘包与拆包；Go Server 采用 goroutine / channel 管理并发客户端和广播；C++ Client 基于 Winsock2 与 `std::thread` 实现异步收发，并完成协议异常、断线清理和局域网通信设计。

## GitHub 发布

发布和 release 前检查见 [docs/github-publishing.md](docs/github-publishing.md)。
