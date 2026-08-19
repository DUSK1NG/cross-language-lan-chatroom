# C++ Windows Client Auto-Reconnect Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 C++ Windows 客户端在服务器断开后保持输入线程可用，并按指数退避自动重连、重新登录和恢复接收。

**Architecture:** 提取一个只负责连接生命周期的 `ConnectionState`，用互斥锁保护当前 `SOCKET`，由接收线程独占连接建立、登录、接收和失效连接回收；主线程只通过线程安全发送函数获取当前 socket，不直接关闭 socket。客户端使用 `running` 控制进程生命周期、`reconnect_enabled` 区分异常断线与主动退出，重连等待使用 1/2/4/8/16/30 秒序列。

**Tech Stack:** C++17、Windows Winsock2、`std::thread`、`std::mutex`、`std::atomic`、MinGW-w64/MSVC、现有长度头 + JSON 协议。

**Spec:** 用户在当前任务中确认的自动重连要求：服务器断开后接收线程不退出；退避为 1/2/4/8/16/30 秒上限；重连成功后重新发送 `login` 并等待 `login_ok`；主线程输入保持可用；主动 `/quit`、输入结束、本地错误禁止重连；显示 `Connection to server lost.` 与 `Reconnecting...`；避免重复关闭同一 socket；最终安全 `join`；保持房间和私聊协议兼容。

## Global Constraints

- 只修改 `client-cpp` 代码、测试和自动重连所需的文档。
- 不修改 Go Server、房间协议、私聊协议或 JSON 字段名称。
- 重连延迟严格使用 1、2、4、8、16、30 秒，并保持 30 秒上限。
- 服务器断开属于可重连状态；用户主动 `/quit`、输入结束、输入错误、Winsock 初始化失败和不可恢复的登录失败属于不可重连状态。
- 任意时刻只能有一个线程创建、替换或关闭当前 socket；主线程不能直接调用 `closesocket`。
- 重连成功必须重新发送原始 `username` 和 `user_code` 的 `login` 消息，并等待 `login_ok` 后才恢复发送普通消息。
- 发送失败只触发连接失效和重连，不把整个客户端进程标记为退出，除非客户端已进入主动关闭状态。
- 测试必须覆盖退避序列、上限、重置和停止重连状态；完成后运行 MinGW 主程序构建、命令测试和协议测试。

---

### Task 1: 提取连接状态与退避策略

**Files:**
- Create: `client-cpp/include/connection.hpp`
- Create: `client-cpp/src/connection.cpp`
- Create: `client-cpp/tests/connection_tests.cpp`
- Modify: `client-cpp/CMakeLists.txt`

**Interfaces:**
- `ConnectionState` 持有当前 socket、服务器地址、登录身份、`std::mutex` 和连接状态；提供 `send(const message::Message&)`、`invalidate()`、`close()`、`is_connected()` 等线程安全操作。
- `ReconnectBackoff::next_delay()` 返回 `std::chrono::seconds`，序列为 `1,2,4,8,16,30,30`；`reset()` 后下一次重新返回 1 秒。
- `connect_and_login()` 负责创建 socket、`connect`、发送 `login`、读取并校验 `login_ok`，失败时关闭临时 socket，不替换旧连接。

- [ ] 写退避策略测试：顺序断言 1/2/4/8/16/30/30，调用 `reset()` 后再次断言 1。
- [ ] 写连接状态测试：初始无连接；替换 socket 后可观察为已连接；`invalidate()` 只使状态失效；`close()` 关闭并恢复无连接状态；并发发送与失效不会访问已关闭句柄。
- [ ] 实现 `connection.hpp/.cpp`，用 `std::lock_guard` 保护 socket 生命周期；连接建立和登录失败都不泄漏句柄。
- [ ] 将新源文件和 `connection-tests` 加入 CMake/CTest。
- [ ] 运行 `connection-tests`，确认新增测试先失败后通过，并提交 `feat: extract reconnectable connection state`。

### Task 2: 将接收线程改为重连循环

**Files:**
- Modify: `client-cpp/src/main.cpp`
- Modify: `client-cpp/include/connection.hpp`
- Modify: `client-cpp/src/connection.cpp`

**Interfaces:**
- `receive_loop` 不再接收裸 socket，而是接收 `ConnectionState&`、`running`、`reconnect_enabled` 和现有输出/私聊上下文。
- 接收失败时依次执行：输出 `Connection to server lost.`；使当前连接失效；若仍允许运行且允许重连，则输出 `Reconnecting...`，等待退避时间，调用 `connect_and_login()`，成功后输出重新连接/登录提示并继续接收；失败后继续下一轮等待。
- 登录失败分为：用户名/代码被占用等服务器明确拒绝时停止重连并退出；网络连接或无法读取响应时继续重连，避免把错误登录循环误认为暂时断线。

- [ ] 先添加/更新逻辑测试所需的纯函数或可注入等待函数，确保退避等待可被停止信号打断。
- [ ] 实现重连循环，重连成功后调用 `ReconnectBackoff::reset()`；每轮失败后才推进下一次退避。
- [ ] 让接收线程只通过 `ConnectionState` 读取和失效连接，不直接 `shutdown` 或 `closesocket` 外部句柄。
- [ ] 保持既有 `chat`、`private_chat`、`system`、`users_response`、房间消息和错误显示逻辑不变。
- [ ] 编译并运行客户端相关测试，提交 `feat: reconnect cpp client after server disconnect`。

### Task 3: 修正主线程发送与退出生命周期

**Files:**
- Modify: `client-cpp/src/main.cpp`
- Modify: `client-cpp/src/connection.cpp`
- Modify: `client-cpp/tests/connection_tests.cpp`

**Interfaces:**
- 主线程发送统一调用 `ConnectionState::send()`；无连接时显示短错误并继续保持输入循环，不解引用无效 socket。
- `/quit`、输入 EOF、控制台读取错误、本地编码错误和不可恢复登录错误都先设置 `reconnect_enabled=false`，再设置 `running=false`。
- 退出顺序固定为：禁止重连 → 设置运行状态 false → 唤醒/关闭当前连接 → 等待接收线程 `join()` → `WSACleanup()`。

- [ ] 将所有主线程发送路径（`/users`、`/rooms`、`/join`、`/leave`、`/msg`、普通聊天、`/quit`）改为 `ConnectionState::send()`。
- [ ] 删除主线程对 socket 的直接 `shutdown`/`closesocket`，只调用连接状态的统一关闭方法。
- [ ] 确认接收线程正在等待重连时，`running=false` 能让等待立即结束，不必等完整 30 秒。
- [ ] 添加状态测试，覆盖主动退出后重连循环不会再次创建连接。
- [ ] 编译 MinGW 主客户端并运行命令/协议/连接测试，提交 `fix: coordinate client shutdown and reconnect`。

### Task 4: 更新文档并完成验证

**Files:**
- Modify: `README.md`
- Modify: `docs/testing.md`
- Modify: `docs/architecture.md`

- [ ] 在 README 的功能、当前限制和运行说明中加入自动重连行为与 1/2/4/8/16/30 秒退避策略。
- [ ] 在测试文档加入 localhost 验收：运行客户端、停止服务器、观察断线提示、等待重连、重新启动服务器、确认重新登录后群聊/私聊/房间命令继续可用。
- [ ] 明确 `/quit`、EOF 和本地错误不会触发重连，避免用户误以为程序卡住。
- [ ] 更新架构图或说明，标明接收线程拥有连接生命周期、主线程通过线程安全接口发送。
- [ ] 运行 `gofmt` 不涉及本次源文件；运行 MinGW 主程序构建、`connection-tests`、`command-tests`、`protocol-tests`，执行 `git diff --check`。
- [ ] 提交 `docs: document cpp client auto reconnect`，整理最终修改和测试结果。

