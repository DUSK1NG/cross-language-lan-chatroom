# Flutter 局域网主机与 CA 自动使用实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 Flutter 连接页展示既有局域网发现结果，并在选择主机后让 C++ 核心自动使用该主机的公开 CA。

**Architecture:** Flutter 从已有 state JSON 的 `lanDiscovery.scanning` 与 `lanDiscovery.hosts` 解析显示元数据。控制器只发送 `session.discoverLanHosts` 和 `session.connectDiscoveredHost`；现有 C++ `LanDiscoveryService` 继续负责发现、证书保存和 TLS 连接，Flutter 不读取证书内容或路径。

**Tech Stack:** Flutter/Dart、现有 Dart FFI `ChatCore`、C++ ChatBridge 协议。

## Global Constraints

- 不扫描本机磁盘，不读取、显示、复制、记录或上传私钥、证书内容、数据库、聊天或日志内容。
- 不修改 C++、QML、React、启动器、安装器或发布流程。
- 仅使用 `session.discoverLanHosts`、`session.connectDiscoveredHost` 与现有 state JSON。
- 每项行为先有 Flutter 失败测试，再作最小实现。

---

### Task 1: 发现状态、控制器命令与连接页面

**Files:**
- Modify: `flutter_client/lib/models/bridge_state.dart`
- Modify: `flutter_client/lib/state/chat_session_controller.dart`
- Modify: `flutter_client/lib/widgets/connection_form.dart`
- Modify: `flutter_client/test/chat_session_controller_test.dart`
- Modify: `flutter_client/test/workspace_page_test.dart`
- Create: `flutter_client/test_driver/lan_discovery_smoke.md`

**Interfaces:**
- Consumes: state JSON `lanDiscovery: {scanning: bool, hosts: []}`。
- Produces: `BridgeDiscoveredHost`、`ChatSessionController.discoverLanHosts()`、`connectDiscoveredHost(String hostId, {required String username, required String userCode})`。

- [ ] **Step 1: 写失败测试**

在 `chat_session_controller_test.dart` 增加 discovery state fixture；断言 `discoverLanHosts()` 分发：

```dart
expect(jsonDecode(core.dispatched.single), {
  'id': startsWith('flutter-'),
  'type': 'session.discoverLanHosts',
  'payload': <String, dynamic>{},
});
```

并断言选择 `host-1` 分发：

```dart
expect(command['type'], 'session.connectDiscoveredHost');
expect(command['payload'], {'hostId': 'host-1', 'username': 'Alice', 'userCode': 'A001'});
```

- [ ] **Step 2: 运行失败测试**

Run: `flutter test test/chat_session_controller_test.dart`

Expected: FAIL，因为发现状态类型和控制器方法尚不存在。

- [ ] **Step 3: 最小实现**

解析 `lanDiscovery.hosts` 的非敏感元数据为 `BridgeDiscoveredHost`。控制器发送上述两个命令；连接表单增加“搜索局域网主机”按钮、扫描状态、主机列表与“使用此主机连接”操作。连接时只沿用已输入的用户名/用户代码；不传递 CA 路径或证书内容。

- [ ] **Step 4: 运行回归与构建**

Run:

```powershell
flutter test
flutter analyze
$env:LAN_CHAT_CORE_DLL = 'C:\Users\Q1573\Desktop\MY_project\lan-chat\.worktrees\flutter-ui-prototype\out\modern-msvc-x64\lan_chat_core.dll'
flutter build windows --debug
git diff --check
```

Expected: 测试和分析通过，生成 Debug 程序，无空白错误。

- [ ] **Step 5: 编写人工验收与提交**

创建 `test_driver/lan_discovery_smoke.md`：说明仅通过 UI 搜索局域网主机、选择发现条目并确认连接；说明 C++ 核心自动管理公开 CA，禁止查看/复制任何证书内容或私钥。随后：

```powershell
git add flutter_client
git commit -m "feat: add Flutter LAN discovery connection"
```

## Self-Review

- 发现、选择连接、无敏感数据边界与人工验收均由 Task 1 覆盖。
- 计划无磁盘扫描、无新 C++ 网络代码、无发布改动。
- `BridgeDiscoveredHost`、控制器方法和命令名称在测试与实现步骤一致。
