# Flutter UI Prototype Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `flutter_client/` 创建可连接现有 LAN Chat 服务端的 Flutter Windows 聊天原型，同时通过 C ABI/FFI 复用现有 C++ 连接逻辑。

**Architecture:** 新增 `lan_chat_core.dll`，其内部托管现有 `GuiChatController` 与 `ChatBridge`，并把 JSON 命令、状态快照、结果和错误转为可轮询的 C ABI 事件队列。Flutter 只依赖该 ABI，负责 Material 3 窗口、状态渲染与输入；QML、React/WebEngine、Go 服务端和发布安装器保持不变。

**Tech Stack:** Flutter stable / Dart、`dart:ffi`、Flutter Material 3、C++20、Qt 6 Core/Network、现有 OpenSSL/TLS、CMake、Qt Test、Flutter test。

## Global Constraints

- 基线提交为 `90b4ce9`；不重置、清理或修改用户未跟踪的 `LANChat-Launcher.exe`。
- Flutter 仅支持 Windows 原型；不修改现有 `LANChat.exe`、QML 默认入口、React/WebEngine 入口、安装器或发布包。
- 不用 Dart 重写 TLS、协议、局域网发现、房主启动、证书或数据库逻辑。
- C ABI 不暴露 QObject、Qt 容器、`std::string` 或 C++ 异常；所有返回字符串由 `lan_chat_core_free_string` 释放。
- C++ 网络线程不能直接调用 Flutter；Flutter UI 线程也不得同步等待网络线程。
- 事件队列最多 256 条；满时仅丢弃可重建的状态事件并保留错误事件。
- 不读取、输出、复制、打包 `%LocalAppData%\DUSK1NG\LAN Chat\host` 内任何文件内容。
- 每个任务必须先写失败测试、运行确认失败、最小实现、运行通过测试，再提交。

---

### Task 1: 建立 Flutter Windows 原型工程与运行环境

**Files:**
- Modify: `flutter_client/README.md`
- Create: `flutter_client/pubspec.yaml`
- Create: `flutter_client/lib/main.dart`
- Create: `flutter_client/test/app_smoke_test.dart`
- Create: `flutter_client/windows/`

**Interfaces:**
- Consumes: Windows、Visual Studio Build Tools、Flutter stable SDK。
- Produces: 名为 `lan_chat_flutter` 的 Flutter Windows 应用；后续任务在 `flutter_client/` 内运行 `flutter test` 与 `flutter run -d windows`。

- [ ] **Step 1: 验证或安装 Flutter stable SDK**

Run:

```powershell
$flutter = Get-Command flutter -ErrorAction SilentlyContinue
if (-not $flutter) {
  throw 'Flutter SDK is required. Install the official Windows stable SDK, add its bin directory to PATH, then reopen PowerShell.'
}
flutter channel stable
flutter doctor -v
```

Expected: `flutter doctor -v` 显示 Flutter、Windows desktop 和 Visual Studio 工具链均可用；Android、Chrome 或模拟器问题不阻塞 Windows 原型。

- [ ] **Step 2: 创建最小 Windows 工程**

Run:

```powershell
Set-Location C:\Users\Q1573\Desktop\MY_project\lan-chat\flutter_client
flutter create --platforms=windows --project-name lan_chat_flutter .
flutter pub add ffi
```

Expected: 生成 `windows/`、`lib/main.dart`、`test/`，`pubspec.yaml` 包含 `ffi` 直接依赖。

- [ ] **Step 3: 写入失败的 Flutter 启动测试**

Create `flutter_client/test/app_smoke_test.dart`:

```dart
import 'package:flutter_test/flutter_test.dart';
import 'package:lan_chat_flutter/main.dart';

void main() {
  testWidgets('shows the Flutter prototype shell', (tester) async {
    await tester.pumpWidget(const LanChatFlutterApp());
    expect(find.text('LAN Chat'), findsOneWidget);
    expect(find.text('未连接'), findsOneWidget);
  });
}
```

- [ ] **Step 4: 运行测试确认失败**

Run: `flutter test test/app_smoke_test.dart`

Expected: FAIL，因为 `LanChatFlutterApp` 或对应文本尚不存在。

- [ ] **Step 5: 实现最小应用壳**

Replace `flutter_client/lib/main.dart` with:

```dart
import 'package:flutter/material.dart';

void main() => runApp(const LanChatFlutterApp());

class LanChatFlutterApp extends StatelessWidget {
  const LanChatFlutterApp({super.key});

  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'LAN Chat',
        theme: ThemeData(colorSchemeSeed: const Color(0xFF4F46E5), useMaterial3: true),
        home: const Scaffold(
          appBar: AppBar(title: Text('LAN Chat')),
          body: Center(child: Text('未连接')),
        ),
      );
}
```

- [ ] **Step 6: 验证并提交**

Run:

```powershell
flutter test test/app_smoke_test.dart
flutter build windows --debug
git add flutter_client
git commit -m "feat: scaffold Flutter Windows prototype"
```

Expected: 测试通过；Windows Debug 构建生成；不改动任何现有客户端目录。

### Task 2: 定义并测试 C ABI 的生命周期与事件队列

**Files:**
- Create: `client-cpp/gui/src/flutter_core/lan_chat_core.h`
- Create: `client-cpp/gui/src/flutter_core/lan_chat_core.cpp`
- Create: `client-cpp/gui/tests/lan_chat_core_tests.cpp`
- Modify: `client-cpp/gui/CMakeLists.txt`

**Interfaces:**
- Consumes: `ChatBridge` 的 `currentStateJson()`、`dispatch(QString)` 和三个 JSON 信号；`GuiChatController` 的线程安全析构行为。
- Produces: `lan_chat_core.dll` 与如下 C ABI，供 Dart 直接绑定：

```c
typedef void* LanChatCoreHandle;
LanChatCoreHandle lan_chat_core_create(void);
void lan_chat_core_destroy(LanChatCoreHandle handle);
int lan_chat_core_dispatch_json(LanChatCoreHandle handle, const char* command_json);
char* lan_chat_core_current_state_json(LanChatCoreHandle handle);
char* lan_chat_core_take_event_json(LanChatCoreHandle handle);
void lan_chat_core_free_string(char* value);
```

返回码固定为 `0`（成功）、`1`（无效句柄）、`2`（无效 UTF-8/JSON）、`3`（Core 正在关闭）。`take_event_json` 无事件时返回空指针。

- [ ] **Step 1: 写入 C ABI 失败测试**

Create `client-cpp/gui/tests/lan_chat_core_tests.cpp`:

```cpp
#include "lan_chat_core.h"
#include <QtTest/QTest>

class LanChatCoreTests final : public QObject {
    Q_OBJECT
private slots:
    void createReturnsStateSnapshot();
    void invalidJsonReturnsValidationError();
    void eventStringCanBeFreedExactlyOnce();
};

void LanChatCoreTests::createReturnsStateSnapshot() {
    auto* handle = lan_chat_core_create();
    QVERIFY(handle != nullptr);
    char* state = lan_chat_core_current_state_json(handle);
    QVERIFY(state != nullptr);
    QVERIFY(QByteArray(state).contains("schemaVersion"));
    lan_chat_core_free_string(state);
    lan_chat_core_destroy(handle);
}
QTEST_MAIN(LanChatCoreTests)
#include "lan_chat_core_tests.moc"
```

- [ ] **Step 2: 运行测试确认失败**

Run:

```powershell
cmake --build .\out\modern-msvc-x64 --target lan-chat-core-tests
ctest --test-dir .\out\modern-msvc-x64 -R lan-chat-core-tests --output-on-failure
```

Expected: FAIL，因为 `lan_chat_core` 目标和头文件尚不存在。

- [ ] **Step 3: 实现唯一的 FFI 边界与队列规则**

Create `lan_chat_core.h` with the declarations above and `extern "C"` guards. In `lan_chat_core.cpp`, implement an owning `CoreSession` that:

```cpp
struct CoreSession {
    QThread thread;
    GuiChatController* controller = nullptr;
    ChatBridge* bridge = nullptr;
    QMutex eventsMutex;
    QQueue<QByteArray> events;
    bool closing = false;
};
```

Construct `GuiChatController` and `ChatBridge` on `thread`; connect `stateChanged`, `commandResult`, and `bridgeError` to a slot that enqueues `{"kind":"state"|"result"|"error","payload":<original-json>}`. Allocate returned `char*` with `malloc`, copy UTF-8 plus NUL, and release only in `lan_chat_core_free_string`. Catch every C++ exception inside each export and return `3` or a queued error JSON; never let an exception cross the ABI.

Add to `client-cpp/gui/CMakeLists.txt`:

```cmake
add_library(lan_chat_core SHARED
    src/flutter_core/lan_chat_core.cpp
    src/flutter_core/lan_chat_core.h
    # reuse the existing ChatBridge/controller/worker sources listed by lan-chat-gui
)
target_link_libraries(lan_chat_core PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Network Threads::Threads OpenSSL::SSL OpenSSL::Crypto ws2_32)
target_include_directories(lan_chat_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
```

Create the analogous `lan-chat-core-tests` executable using the same Core, controller, worker, protocol and OpenSSL sources as `chat-bridge-tests`; link it to `Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test Qt6::Network`, register it as CTest test `lan-chat-core-tests`, and give it the same Qt/OpenSSL PATH properties. Add `destroyStopsCoreWithinFiveSeconds` before implementing the Core:

```cpp
void LanChatCoreTests::destroyStopsCoreWithinFiveSeconds() {
    auto* handle = lan_chat_core_create();
    QVERIFY(handle != nullptr);
    QElapsedTimer timer;
    timer.start();
    lan_chat_core_destroy(handle);
    QVERIFY2(timer.elapsed() <= 5000, "Core shutdown exceeded five seconds");
}
```

- [ ] **Step 4: 验证 C ABI 生命周期和 JSON 校验**

Run:

```powershell
cmake --build .\out\modern-msvc-x64 --target lan-chat-core lan-chat-core-tests
ctest --test-dir .\out\modern-msvc-x64 -R lan-chat-core-tests --output-on-failure
```

Expected: 测试通过；`lan_chat_core.dll` 生成；无 Qt 对象、线程或字符串跨 ABI 泄漏。

- [ ] **Step 5: 提交**

```powershell
git add client-cpp/gui/CMakeLists.txt client-cpp/gui/src/flutter_core client-cpp/gui/tests/lan_chat_core_tests.cpp
git commit -m "feat: expose LAN Chat core through C ABI"
```

### Task 3: 绑定 C ABI 到 Dart 并验证内存所有权

**Files:**
- Create: `flutter_client/lib/native/lan_chat_core_bindings.dart`
- Create: `flutter_client/lib/native/lan_chat_core.dart`
- Create: `flutter_client/test/lan_chat_core_test.dart`
- Modify: `flutter_client/windows/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 2 的 `lan_chat_core.dll` 和六个导出函数。
- Produces: `abstract interface class ChatCore` 与其生产实现 `LanChatCore`。接口固定为 `String stateJson()`、`int dispatch(String json)`、`List<String> drainEvents()`、`void dispose()`；任何 UI 层只通过 `ChatCore` 访问原生库。

- [ ] **Step 1: 写入失败的 Dart FFI 测试**

Create `flutter_client/test/lan_chat_core_test.dart`:

```dart
import 'package:flutter_test/flutter_test.dart';
import 'package:lan_chat_flutter/native/lan_chat_core.dart';

void main() {
  test('reads a schema-versioned initial snapshot', () {
    final core = LanChatCore.openForTest();
    addTearDown(core.dispose);
    expect(core.stateJson(), contains('"schemaVersion":1'));
  });
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `flutter test test/lan_chat_core_test.dart`

Expected: FAIL，因为 `LanChatCore` 尚不存在。

- [ ] **Step 3: 实现最小绑定和安全释放**

Create `lan_chat_core_bindings.dart` with `dart:ffi` typedefs matching the six exports. In `lan_chat_core.dart`, declare `ChatCore`, then implement `LanChatCore implements ChatCore` so each returned `Pointer<Char>` is decoded with `toDartString()` in a `try` block and always released in `finally` through `lan_chat_core_free_string`.

```dart
String _takeString(Pointer<Char> pointer) {
  if (pointer == nullptr) return '';
  try {
    return pointer.cast<Utf8>().toDartString();
  } finally {
    _bindings.lanChatCoreFreeString(pointer);
  }
}
```

`windows/CMakeLists.txt` must copy the exact built DLL beside the Flutter executable after the runner target is built:

```cmake
add_custom_command(TARGET ${BINARY_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${LAN_CHAT_CORE_DLL}"
    "$<TARGET_FILE_DIR:${BINARY_NAME}>/lan_chat_core.dll")
```

In `windows/CMakeLists.txt`, define `LAN_CHAT_CORE_DLL` from `$ENV{LAN_CHAT_CORE_DLL}` and fail CMake configure when it is empty or missing. In Dart, load the copied `lan_chat_core.dll` beside `Platform.resolvedExecutable`; `openForTest()` may load the absolute path from the same environment variable. Do not search user data directories.

- [ ] **Step 4: 验证 Dart 测试与 Windows 运行时复制**

Run:

```powershell
$env:LAN_CHAT_CORE_DLL = 'C:\Users\Q1573\Desktop\MY_project\lan-chat\out\modern-msvc-x64\lan_chat_core.dll'
flutter test test/lan_chat_core_test.dart
flutter run -d windows --debug
```

Expected: 测试通过；Windows 应用启动后能读取 JSON 状态；退出时没有 Access Violation 或双重释放。

- [ ] **Step 5: 提交**

```powershell
git add flutter_client/lib flutter_client/test flutter_client/windows/CMakeLists.txt flutter_client/pubspec.yaml flutter_client/pubspec.lock
git commit -m "feat: bind Flutter prototype to LAN Chat core"
```

### Task 4: 实现 Flutter 聊天状态与窗口骨架

**Files:**
- Create: `flutter_client/lib/models/bridge_state.dart`
- Create: `flutter_client/lib/state/chat_session_controller.dart`
- Create: `flutter_client/lib/widgets/connection_banner.dart`
- Create: `flutter_client/lib/widgets/conversation_sidebar.dart`
- Create: `flutter_client/lib/widgets/message_timeline.dart`
- Create: `flutter_client/lib/widgets/message_composer.dart`
- Modify: `flutter_client/lib/main.dart`
- Create: `flutter_client/test/chat_session_controller_test.dart`
- Create: `flutter_client/test/workspace_page_test.dart`

**Interfaces:**
- Consumes: `LanChatCore.stateJson()` 和 `LanChatCore.drainEvents()` 的 schemaVersion 1 JSON。
- Produces: `ChatSessionController extends ChangeNotifier`；其公开值为 `connectionPhase`、`statusText`、`conversations`、`messages`、`lastError` 与 `sendMessage(String)`。

- [ ] **Step 1: 写入失败的状态解析和发送测试**

Create `chat_session_controller_test.dart`:

```dart
import 'package:flutter_test/flutter_test.dart';
import 'package:lan_chat_flutter/native/lan_chat_core.dart';
import 'package:lan_chat_flutter/state/chat_session_controller.dart';

void main() {
  test('maps a connected bridge snapshot and sends through the selected room', () {
    final core = FakeLanChatCore(snapshot: connectedLobbyStateJson);
    final controller = ChatSessionController(core)..refresh();
    expect(controller.connectionPhase, 'connected');
    expect(controller.conversations.single.title, 'lobby');
    controller.sendMessage('hello');
    expect(core.dispatched.single, contains('chat.sendRoom'));
  });
}
```

Define the test-only fake in the same test file:

```dart
class FakeLanChatCore implements ChatCore {
  FakeLanChatCore({required this.snapshot});
  String snapshot;
  final dispatched = <String>[];
  @override String stateJson() => snapshot;
  @override int dispatch(String json) { dispatched.add(json); return 0; }
  @override List<String> drainEvents() => const [];
  @override void dispose() {}
}

const connectedLobbyStateJson = '{"schemaVersion":1,"connection":{"phase":"connected","statusText":"已连接"},"rooms":[{"roomName":"lobby","memberCount":1,"unreadCount":0}],"directMessages":[],"activeMessages":[]}';
```

- [ ] **Step 2: 运行测试确认失败**

Run: `flutter test test/chat_session_controller_test.dart`

Expected: FAIL，因为状态模型、`FakeLanChatCore` 或 controller 尚不存在。

- [ ] **Step 3: 实现 JSON 状态映射和轮询**

Implement `BridgeState.fromJson(Map<String, dynamic>)` with an explicit `schemaVersion == 1` check. `ChatSessionController` must call `drainEvents()` every 100 ms only while the app is resumed, coalesce multiple state events into one `notifyListeners()`, and surface malformed JSON as a non-throwing `lastError`.

`sendMessage` must reject blank text locally and dispatch exactly:

```dart
{
  'id': 'flutter-<monotonic-counter>',
  'type': 'chat.sendRoom',
  'payload': {'content': text.trim(), 'room': selectedRoom},
}
```

- [ ] **Step 4: 实现可访问的三栏窗口**

`main.dart` must use `ChangeNotifierProvider`-free standard Flutter primitives (`AnimatedBuilder` or `ListenableBuilder`) so no new state package is added. Layout requirements:

```text
AppBar: LAN Chat + ConnectionBanner
Row: ConversationSidebar (240px) | Expanded(MessageTimeline + MessageComposer)
```

`MessageComposer` sends with Enter, inserts a newline with Shift+Enter, and disables Send while disconnected. All buttons require `Tooltip` and semantic labels.

- [ ] **Step 5: 验证部件和状态测试**

Run:

```powershell
flutter test test/chat_session_controller_test.dart test/workspace_page_test.dart
flutter analyze
```

Expected: 所有测试和静态分析通过；不需要真实服务端即可覆盖状态、消息渲染和发送命令。

- [ ] **Step 6: 提交**

```powershell
git add flutter_client/lib flutter_client/test
git commit -m "feat: add Flutter chat workspace prototype"
```

### Task 5: 接入真实服务端并执行完整回归

**Files:**
- Create: `flutter_client/test_driver/real_connection_smoke.md`
- Modify: `flutter_client/README.md`
- Modify: `docs/testing.md`

**Interfaces:**
- Consumes: Flutter workspace、`lan_chat_core.dll`、现有 `server-go/chat-server.exe` 和 TLS host 路径。
- Produces: 可重复的 Windows 端到端验收命令；Flutter 原型继续使用独立构建命令，不改变现有构建脚本。

- [ ] **Step 1: 编写真实连接烟测文档**

Document an exact two-client check in `flutter_client/test_driver/real_connection_smoke.md`: start one existing LAN Chat host, start Flutter with `serverIp`, `serverPort`, `username`, `userCode`, and public CA path through its connect command, send `flutter-smoke-<timestamp>`, verify the existing client receives it, then close Flutter and confirm only the Flutter Core connection stops. The document must direct the operator to use only public CA metadata and must not print or copy host private-key, certificate, database, chat or log contents.

- [ ] **Step 2: 验证全部回归与人工 Windows 场景**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Test -QtPrefix .\.tools\qt\6.10.3\msvc2022_64 -OpenSslRoot .\.tools\vcpkg\installed\x64-windows
Set-Location .\flutter_client
flutter test
flutter analyze
```

Expected: 既有构建和 CTest 全部通过；Flutter 测试/分析通过。人工验证窗口缩放、高 DPI、Enter/Shift+Enter、连接失败、断线提示、双向收发与关闭回收。

- [ ] **Step 3: 提交**

```powershell
git add client-cpp/gui/src/flutter_core client-cpp/gui/tests/lan_chat_core_tests.cpp flutter_client docs/testing.md
git commit -m "test: verify Flutter core connection lifecycle"
```

## Self-Review

- 规格的独立目录、C ABI、256 条事件队列、5 秒关闭、Flutter 三栏原型、真实现有连接、保留旧 UI、数据保护和回滚均分别由 Tasks 1-5 覆盖。
- 每项源代码变更均有先失败再通过的 C++ 或 Dart 测试；真实服务端收发保持为明确的人工 smoke 验收，不伪造为单元测试。
- 所有 C ABI 名称、返回码和 Dart wrapper 名称在 Tasks 2-5 保持一致；Flutter 不调用 Qt/QML 对象。
- 本计划不包含安装器切换、发布打包或 Dart 重写网络协议，符合原型范围。
