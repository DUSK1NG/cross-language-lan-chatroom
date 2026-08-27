# Flutter Resize Polling A/B Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 生成 UI 完全相同、仅 Core 定时轮询开关不同的两个 Windows Release 诊断版本。

**Architecture:** `LanChatFlutterApp` 接收默认启用的 `startPolling` 参数，并将它传入已有的 `ChatSessionController`。正式入口通过编译期 `LAN_CHAT_DISABLE_POLLING` Dart define 生成暂停轮询的诊断版本；未提供 define 时保持现有正式行为。

**Tech Stack:** Flutter 3.47.1、Dart 3.13.1、flutter_test、Windows Release、现有 C FFI Core。

## Global Constraints

- 不修改协议、原生 Core 行为或旧客户端。
- 默认构建必须继续启用 100ms Core 轮询。
- 暂停轮询版本只用于缩放诊断，不作为正式程序交付。
- 两个版本必须使用相同 Core、Qt 和 OpenSSL 运行库。
- 不读取、复制、输出或散列证书、私钥、数据库、聊天记录或日志内容。

---

### Task 1: 增加可测试的诊断轮询开关

**Files:**
- Modify: `flutter_client/test/workspace_page_test.dart`
- Modify: `flutter_client/lib/main.dart`

**Interfaces:**
- Consumes: `ChatSessionController(ChatCore core, {bool startPolling = true})`。
- Produces: `LanChatFlutterApp({ChatCore? core, String? startupError, bool startPolling = true, Key? key})` 与编译期 `LAN_CHAT_DISABLE_POLLING` 开关。

- [ ] **Step 1: 编写失败测试**

在 `_FakeCore` 增加 `int drainEventsCallCount = 0`，并在 `drainEvents()` 中递增。新增测试：

```dart
testWidgets('can disable periodic core polling for resize diagnosis', (
  tester,
) async {
  final core = _FakeCore(snapshot: _idleStateJson);

  await tester.pumpWidget(
    LanChatFlutterApp(core: core, startPolling: false),
  );
  await tester.pump(const Duration(milliseconds: 350));

  expect(core.drainEventsCallCount, 0);
});
```

- [ ] **Step 2: 运行测试并确认 RED**

Run:
```powershell
Push-Location flutter_client
flutter test test/workspace_page_test.dart --plain-name "can disable periodic core polling for resize diagnosis"
Pop-Location
```
Expected: 编译失败，提示 `startPolling` 不是 `LanChatFlutterApp` 的命名参数。

- [ ] **Step 3: 实现最小开关**

在 `flutter_client/lib/main.dart` 增加：

```dart
const _disableCorePolling = bool.fromEnvironment(
  'LAN_CHAT_DISABLE_POLLING',
);
```

正式入口传入：

```dart
runApp(
  LanChatFlutterApp(
    core: LanChatCore.open(),
    startPolling: !_disableCorePolling,
  ),
);
```

`LanChatFlutterApp` 增加字段和默认值：

```dart
const LanChatFlutterApp({
  this.core,
  this.startupError,
  this.startPolling = true,
  super.key,
});

final bool startPolling;
```

初始化控制器时传入：

```dart
_session = ChatSessionController(
  core,
  startPolling: widget.startPolling,
)..refresh();
```

- [ ] **Step 4: 运行目标测试并确认 GREEN**

Run:
```powershell
Push-Location flutter_client
flutter test test/workspace_page_test.dart --plain-name "can disable periodic core polling for resize diagnosis"
Pop-Location
```
Expected: `All tests passed!`。

- [ ] **Step 5: 增加默认行为回归测试**

新增测试：

```dart
testWidgets('keeps periodic core polling enabled by default', (tester) async {
  final core = _FakeCore(snapshot: _idleStateJson);

  await tester.pumpWidget(LanChatFlutterApp(core: core));
  await tester.pump(const Duration(milliseconds: 350));

  expect(core.drainEventsCallCount, greaterThanOrEqualTo(3));
});
```

Run:
```powershell
Push-Location flutter_client
flutter test test/workspace_page_test.dart --plain-name "keeps periodic core polling enabled by default"
Pop-Location
```
Expected: `All tests passed!`。

- [ ] **Step 6: 运行全量验证并提交**

Run:
```powershell
Push-Location flutter_client
flutter test
flutter analyze
ctest --test-dir ../out/flutter-core-vs2022 -C Release -R lan-chat-core --output-on-failure
Pop-Location
git diff --check
```
Expected: Flutter 45 项通过、1 项受控原生测试跳过；静态分析无问题；Core 2/2 通过；差异检查通过。

Commit:
```powershell
git add flutter_client/lib/main.dart flutter_client/test/workspace_page_test.dart
git commit -m "test: add Flutter resize polling comparison"
```

### Task 2: 构建并验证 A/B Release 目录

**Files:**
- Generate: `out/resize-ab/polling-on/`
- Generate: `out/resize-ab/polling-off/`
- Restore: `flutter_client/build/windows/x64/runner/Release/`

**Interfaces:**
- Consumes: `LAN_CHAT_DISABLE_POLLING` Dart define 与 `LAN_CHAT_CORE_DLL`、`LAN_CHAT_QT_PREFIX`、`LAN_CHAT_OPENSSL_ROOT` 环境变量。
- Produces: 两个可独立运行的 `lan_chat_flutter.exe` 诊断目录，主 Release 目录最终恢复默认轮询版本。

- [ ] **Step 1: 构建并保存 A 版**

Run:
```powershell
Push-Location flutter_client
flutter build windows --release
Pop-Location
Copy-Item flutter_client/build/windows/x64/runner/Release out/resize-ab/polling-on -Recurse
```
Expected: `out/resize-ab/polling-on/lan_chat_flutter.exe` 存在。

- [ ] **Step 2: 构建并保存 B 版**

Run:
```powershell
Push-Location flutter_client
flutter build windows --release --dart-define=LAN_CHAT_DISABLE_POLLING=true
Pop-Location
Copy-Item flutter_client/build/windows/x64/runner/Release out/resize-ab/polling-off -Recurse
```
Expected: `out/resize-ab/polling-off/lan_chat_flutter.exe` 存在。

- [ ] **Step 3: 恢复主 Release 为默认轮询版**

Run:
```powershell
Push-Location flutter_client
flutter build windows --release
Pop-Location
```
Expected: `flutter_client/build/windows/x64/runner/Release/lan_chat_flutter.exe` 为未设置诊断 define 的正式构建。

- [ ] **Step 4: 验证 A/B 运行时**

分别隐藏启动两个 EXE 3 秒，确认进程未提前退出；检查 `lan_chat_core.dll`、`libssl-3-x64.dll`、`libcrypto-3-x64.dll`、`Qt6Core.dll`、`Qt6Gui.dll`、`Qt6Network.dll`、`Qt6Quick.dll`、`Qt6Qml.dll` 均从对应诊断目录加载，然后仅停止各自测试进程。

Expected: A、B 均为 `已加载目标运行库: 8/8`，目录外运行库为 0。

- [ ] **Step 5: 检查仓库状态并交付**

Run:
```powershell
git diff --check
git status --short --branch
```
Expected: 除用户已有的 `LANChat-Launcher.exe` 外无未提交源码修改。向用户提供 A、B 两个 EXE 的绝对路径，并说明 B 版不接收 Core 事件，只用于缩放比较。
