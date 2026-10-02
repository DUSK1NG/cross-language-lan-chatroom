# Flutter 全功能 UI 保留实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 Flutter Windows UI 覆盖原有三种连接入口、聊天和管理能力，同时完全复用现有 C++ 核心。

**Architecture:** Dart 只向 `ChatCore.dispatch` 发既有 Bridge JSON 命令，并从状态快照渲染 UI。C++ Core 保持 TLS、局域网发现、证书持久化、服务端控制和网络线程所有权；Windows runner 显式打包与 Core ABI 匹配的 Qt/OpenSSL 运行库。

**Tech Stack:** Flutter/Dart、Dart FFI、C++ Qt 6.10.3、OpenSSL 3.6.3、现有 ChatBridge。

## Global Constraints

- 只替换 Flutter 前端；不得修改 QML、React、Go 服务端、网络协议、启动器、安装器或发布流程。
- 不读取、显示、复制、哈希、记录或上传私钥、证书内容、数据库、聊天或日志；本地主机 UI 仅传递用户选择的路径。
- 不扫描磁盘寻找 CA；局域网连接只使用既有 `session.discoverLanHosts` 与 `session.connectDiscoveredHost`。
- 每项 UI/控制器行为先有失败 Flutter 测试；每任务含 Windows Debug 构建、`flutter test`、`flutter analyze` 和 `git diff --check`。

---

### Task 1: 修复 Windows Core DLL 运行时依赖

**Files:**
- Modify: `flutter_client/windows/runner/CMakeLists.txt`
- Modify: `flutter_client/windows/CMakeLists.txt`
- Create: `flutter_client/test_driver/windows_core_runtime_smoke.md`
- Modify: `flutter_client/test/lan_chat_core_test.dart`

**Interfaces:**
- Consumes: `LAN_CHAT_CORE_DLL`、Qt 6.10.3 bin、OpenSSL 3.6.3 DLL。
- Produces: Debug runner 目录中可被 `DynamicLibrary.open` 加载的 `lan_chat_core.dll` 及其精确依赖。

- [ ] **Step 1: 写失败的加载诊断测试与运行说明**

增加只在 `LAN_CHAT_CORE_DLL` 存在时运行的测试，调用 `LanChatCore.openForTest()` 并断言 `stateJson()` 产生 schemaVersion；烟测文档要求在新进程执行，不复用旧窗口。

- [ ] **Step 2: 运行 RED**

Run: `flutter test test/lan_chat_core_test.dart`

Expected: 在当前失败运行目录中报告加载错误，不允许用 idle fallback 通过。

- [ ] **Step 3: 最小 CMake 运行时打包**

在 runner `POST_BUILD` 中从 QtPrefix 复制 `Qt6Core.dll`、`Qt6Gui.dll`、`Qt6Network.dll`、`Qt6Quick.dll`，从 OpenSSL 根复制 `libssl-3-x64.dll` 与 `libcrypto-3-x64.dll`；仅当文件存在时配置，缺失则 CMake `FATAL_ERROR`。

- [ ] **Step 4: GREEN 与提交**

```powershell
$env:LAN_CHAT_CORE_DLL = 'C:\Users\Q1573\Desktop\MY_project\lan-chat\.worktrees\flutter-ui-prototype\out\modern-msvc-x64\lan_chat_core.dll'
flutter test test/lan_chat_core_test.dart
flutter build windows --debug
git add flutter_client
git commit -m "fix: package Flutter core runtime dependencies"
```

Expected: Core 加载测试通过，新进程显示连接页而无 `error code: 127`。

### Task 2: 模式选择与三种连接流程

**Files:**
- Create: `flutter_client/lib/widgets/mode_selection_page.dart`
- Create: `flutter_client/lib/widgets/local_host_form.dart`
- Modify: `flutter_client/lib/main.dart`
- Modify: `flutter_client/lib/state/chat_session_controller.dart`
- Modify: `flutter_client/test/chat_session_controller_test.dart`
- Modify: `flutter_client/test/workspace_page_test.dart`

**Interfaces:**
- Produces: `connectLocalHost({serverExe, certFile, keyFile, dbFile, username, userCode})`，发送 `session.connectLocalHost`；模式选择仅导航。

- [ ] **Step 1: 写失败测试**

断言初始页显示“远程服务器”“创建本地聊天室”“加入局域网聊天室”；断言本地主机命令为：

```dart
expect(command['type'], 'session.connectLocalHost');
expect(command['payload'].keys, containsAll(<String>['serverExe','certFile','keyFile','dbFile','username','userCode']));
```

- [ ] **Step 2: 运行 RED**

Run: `flutter test test/workspace_page_test.dart test/chat_session_controller_test.dart`

Expected: FAIL，因为选择页和本地主机控制器接口不存在。

- [ ] **Step 3: 最小实现**

创建三张入口卡。远程复用既有表单；局域网复用既有发现表单；本地主机表单仅显示六个路径/身份输入值，点击后只传路径字符串，绝不读取文件内容。

- [ ] **Step 4: GREEN 与提交**

```powershell
flutter test
flutter analyze
flutter build windows --debug
git add flutter_client
git commit -m "feat: restore Flutter connection mode selection"
```

### Task 3: 聊天会话、频道与消息操作入口

**Files:**
- Modify: `flutter_client/lib/models/bridge_state.dart`
- Modify: `flutter_client/lib/state/chat_session_controller.dart`
- Create: `flutter_client/lib/widgets/room_actions.dart`
- Modify: `flutter_client/lib/widgets/message_timeline.dart`
- Modify: `flutter_client/test/chat_session_controller_test.dart`
- Modify: `flutter_client/test/workspace_page_test.dart`

**Interfaces:**
- Produces: 对既有 `chat.sendRoom`、`chat.sendPrivate`、`conversation.openPrivate`、`room.create`、`room.action`、`message.copy`、`message.removeLocal`、`message.recall`、`message.retry` 的严格 payload 调度。

- [ ] **Step 1: 写失败测试**

为房间创建、私聊发送和消息重试加入 fake-core 断言，例如：

```dart
expect(command['type'], 'room.create');
expect(command['payload'], {'room': 'study'});
```

- [ ] **Step 2: 运行 RED**

Run: `flutter test test/chat_session_controller_test.dart`

Expected: FAIL，因为这些控制器方法不存在。

- [ ] **Step 3: 最小实现与 GREEN**

将控制器方法逐项映射至现有 Bridge 协议；UI 仅在 state 授权/状态允许时显示相应操作。复制只调用既有 `message.copy`，不记录消息；操作结果与错误由现有事件状态显示。

```powershell
flutter test
flutter analyze
git add flutter_client
git commit -m "feat: restore Flutter chat and room actions"
```

### Task 4: 目录、管理、设置与端到端验收

**Files:**
- Create: `flutter_client/lib/widgets/settings_page.dart`
- Create: `flutter_client/lib/widgets/admin_actions.dart`
- Modify: `flutter_client/lib/state/chat_session_controller.dart`
- Modify: `flutter_client/test/chat_session_controller_test.dart`
- Modify: `flutter_client/test_driver/real_connection_smoke.md`
- Modify: `docs/testing.md`

**Interfaces:**
- Produces: `directory.refreshUsers`、`directory.refreshRooms`、`admin.action`、`settings.setPerformanceMode`、`settings.setConnectionLogging` 的现有 JSON 协议调用。

- [ ] **Step 1: 写失败测试与运行 RED**

断言设置和目录命令 payload：

```dart
expect(command['type'], 'settings.setPerformanceMode');
expect(command['payload'], {'mode': 'balanced'});
```

Run: `flutter test test/chat_session_controller_test.dart`

Expected: FAIL，因为目录、管理和设置方法不存在。

- [ ] **Step 2: 最小实现与 GREEN**

实现设置页和状态允许的管理入口；不显示敏感数据。文档描述由操作员提供地址和公开 CA 的双客户端验收，不输出私钥、证书内容、数据库、聊天或日志。

- [ ] **Step 3: 全量验证与提交**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Test -QtPrefix .\.tools\qt\6.10.3\msvc2022_64 -OpenSslRoot .\.tools\vcpkg\installed\x64-windows
flutter test
flutter analyze
flutter build windows --debug
git diff --check
git add flutter_client docs/testing.md
git commit -m "feat: complete Flutter UI parity"
```

Expected: 既有 C++ 测试、Flutter 测试和 Debug 构建均通过；真实双客户端仅按人工验收执行。

### Task 5: 连接审批保留

**Files:**
- Modify: `flutter_client/lib/models/bridge_state.dart`
- Modify: `flutter_client/lib/state/chat_session_controller.dart`
- Modify: `flutter_client/lib/widgets/admin_actions.dart`
- Modify: `flutter_client/test/chat_session_controller_test.dart`
- Modify: `flutter_client/test/workspace_page_test.dart`

**Interfaces:**
- Consumes: state JSON 的 `connectionApprovals` 条目与现有 `admin.action`。
- Produces: 管理员可见的批准/拒绝入口，分别分发 `{"action":"approve_connection","messageId":...}` 和 `{"action":"deny_connection","messageId":...}`。

- [x] **Step 1: 写失败测试并运行 RED**

加入带 `connectionApprovals` 的管理员 state fixture，断言非管理员没有审批控件；断言管理员操作精确分发：

```dart
expect(command['type'], 'admin.action');
expect(command['payload'], {'action': 'approve_connection', 'messageId': 'approval-1'});
```

Run: `flutter test test/chat_session_controller_test.dart test/workspace_page_test.dart`

Expected: FAIL，因为 state 未解析审批项且控制器/API 不存在。

- [x] **Step 2: 最小实现与 GREEN**

解析审批条目的非敏感展示字段和 messageId；只在已连接管理员状态渲染批准/拒绝按钮。控制器在分发前再次检查管理员与已连接状态。不得显示或存储审批消息以外的聊天、证书或凭据。

- [ ] **Step 3: 验证与提交**

```powershell
flutter test
flutter analyze
$env:LAN_CHAT_CORE_DLL = 'C:\Users\Q1573\Desktop\MY_project\lan-chat\.worktrees\flutter-ui-parity-clean\out\modern-msvc-x64\lan_chat_core.dll'
flutter build windows --debug
git diff --check
git add flutter_client docs/superpowers/plans/2026-08-27-flutter-full-ui-parity.md
git commit -m "feat: add Flutter connection approvals"
```


## Self-Review

- Task 1 覆盖截图中的 DLL 加载故障；Task 2 覆盖缺失选择页和三种连接入口；Task 3、4 覆盖既有聊天、管理和设置命令。
- 每一项均使用既有 Bridge 命令，不创建网络、TLS 或证书新实现。
- 计划没有磁盘扫描，也没有读取敏感内容的步骤。
