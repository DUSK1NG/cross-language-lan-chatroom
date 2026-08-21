# React/QWebEngine UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first production-ready seam for a React/TypeScript UI inside the existing Qt client, while preserving QML fallback and all existing Go/C++ networking and business logic.

**Architecture:** Add a small JSON protocol module and a thin `ChatBridge(QObject)` above `GuiChatController`. React communicates only through QWebChannel. Qt selects React or the existing QML loader at startup; React is loaded from Vite during development and embedded qrc resources in release.

**Tech Stack:** C++20, Qt 6.11, Qt WebEngine Quick, Qt WebChannel, Qt Test, React, TypeScript, Vite, Tailwind CSS, Motion, Lucide, Vitest, React Testing Library.

**Spec:** `docs/superpowers/specs/2026-08-21-react-webengine-ui-design.md`

## Global Constraints

- Preserve the Go TLS/TCP server, 4-byte big-endian framing, JSON protocol, C++ Socket/TLS, worker thread, SQLite and controller business rules.
- React calls C++ only through the `chatBridge` QWebChannel object.
- Never put passwords, private-key contents, sockets, thread handles or database contents into React state, JSON snapshots, logs or test fixtures.
- Use `http://127.0.0.1:5173` only for development and `qrc:/frontend/index.html` for release.
- Keep QML fallback until real TLS two-client acceptance passes.
- Use Chinese human-facing copy, stable English bridge keys, Aurora Glass tokens, high contrast and restrained blur.
- Prefer `transform` and `opacity`; do not use realtime blur on message lists or message items.
- Keep each task independently buildable or testable and commit only that task's files.
- Current dependency gate: `C:\Qt\6.11.2\mingw_64` currently has Quick but no `Qt6WebEngine*` or `Qt6WebChannel` CMake package. WebEngine tasks must remain disabled until those components are installed and verified.

---

### Task 1: Define and test the bridge JSON protocol

**Files:**
- Create: `client-cpp/gui/src/bridge_protocol.hpp`
- Create: `client-cpp/gui/src/bridge_protocol.cpp`
- Create: `client-cpp/gui/tests/bridge_protocol_tests.cpp`
- Modify: `client-cpp/gui/CMakeLists.txt`

**Interfaces:**
- Produces `bridge::validateCommand(const QJsonObject&, QString*) -> bool`.
- Produces `bridge::makeCommandResult(const QString&, bool, const QJsonObject&) -> QJsonObject`.
- Produces `bridge::makeError(const QString&, const QString&, bool, const QString&, const QString&) -> QJsonObject`.
- Produces `bridge::serializeState(const QJsonObject&) -> QString`, which adds `schemaVersion: 1` and returns compact UTF-8 JSON.
- Consumes no controller or network object; this module is pure Qt JSON logic and is tested independently.

- [ ] **Step 1: Add the failing protocol tests**

Add Qt Test cases for:

```cpp
void BridgeProtocolTests::acceptsRoomMessageCommand() {
    const QJsonObject command{
        {"id", "cmd-42"},
        {"type", "chat.sendRoom"},
        {"payload", QJsonObject{{"content", "你好"}, {"room", "lobby"}}}
    };
    QString error;
    QVERIFY(bridge::validateCommand(command, &error));
    QVERIFY(error.isEmpty());
}

void BridgeProtocolTests::rejectsMissingIdAndPayload() {
    QString error;
    QVERIFY(!bridge::validateCommand(QJsonObject{{"type", "chat.sendRoom"}}, &error));
    QCOMPARE(error, QStringLiteral("invalid_command"));
}

void BridgeProtocolTests::neverSerializesPassword() {
    const QString json = bridge::serializeState(QJsonObject{
        {"connection", QJsonObject{{"phase", "connected"}}},
        {"savedConnection", QJsonObject{{"username", "Alice"}}}
    });
    QVERIFY(!json.contains(QStringLiteral("password"), Qt::CaseInsensitive));
    QVERIFY(!json.contains(QStringLiteral("privateKey"), Qt::CaseInsensitive));
}

void BridgeProtocolTests::createsStableErrorEnvelope() {
    const QJsonObject error = bridge::makeError(
        "connection_failed", "无法连接到服务器", true, "controller", "cmd-42");
    QCOMPARE(error.value("code").toString(), QStringLiteral("connection_failed"));
    QCOMPARE(error.value("retryable").toBool(), true);
    QCOMPARE(error.value("commandId").toString(), QStringLiteral("cmd-42"));
}
```

Include cases for all command type prefixes in the spec: session, chat, conversation, directory, room, admin and message. Required payload validation must reject wrong JSON types and empty required strings without implementing server rules.

- [ ] **Step 2: Run the new test target and verify it fails**

Run:

```powershell
$env:Path = 'C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.2\mingw_64\bin;' + $env:Path
cmake -S .\client-cpp\gui -B .\client-cpp\gui\build-bridge -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_CXX_COMPILER='C:\Qt\Tools\mingw1310_64\bin\g++.exe' `
  -DCMAKE_PREFIX_PATH='C:\Qt\6.11.2\mingw_64'
cmake --build .\client-cpp\gui\build-bridge --target bridge-protocol-tests
ctest --test-dir .\client-cpp\gui\build-bridge -R bridge-protocol-tests --output-on-failure
```

Expected: configuration succeeds, compilation fails because the protocol functions do not exist yet.

- [ ] **Step 3: Implement the minimal JSON module**

Use `QJsonObject`, `QJsonDocument` and `QString`. `validateCommand` checks exactly three top-level fields (`id`, `type`, `payload`), non-empty `id`/`type`, object payload, and the required fields for each command family. It returns the literal error code `invalid_command` for shape failures. It does not validate permissions or server-side business rules.

`makeCommandResult` returns `{id, ok}` and adds `error` only when `ok` is false. `makeError` returns the error object from the spec. `serializeState` adds `schemaVersion` only when absent and never mutates the caller's object.

- [ ] **Step 4: Register the standalone test target**

Modify `client-cpp/gui/CMakeLists.txt` to add a `bridge-protocol-tests` executable using `bridge_protocol.cpp` and `bridge_protocol_tests.cpp`, link `Qt6::Core` and `Qt6::Test`, and register it with CTest. Keep the production GUI target unchanged except for compiling the protocol source into it.

- [ ] **Step 5: Run the tests and verify they pass**

Run the configure, build and CTest commands above. Expected: all bridge protocol cases pass and the existing QML GUI target still builds.

- [ ] **Step 6: Commit the protocol seam**

```powershell
git add client-cpp/gui/src/bridge_protocol.hpp client-cpp/gui/src/bridge_protocol.cpp client-cpp/gui/tests/bridge_protocol_tests.cpp client-cpp/gui/CMakeLists.txt
git commit -m "test(gui): define bridge JSON protocol"
```

### Task 2: Implement ChatBridge state snapshots and command dispatch

**Files:**
- Create: `client-cpp/gui/src/chat_bridge.hpp`
- Create: `client-cpp/gui/src/chat_bridge.cpp`
- Create: `client-cpp/gui/tests/chat_bridge_tests.cpp`
- Modify: `client-cpp/gui/src/gui_chat_controller.hpp`
- Modify: `client-cpp/gui/src/gui_chat_controller.cpp`
- Modify: `client-cpp/gui/CMakeLists.txt`

**Interfaces:**
- `ChatBridge(QObject* parent, GuiChatController* controller)` owns no controller and never owns the worker thread.
- `Q_INVOKABLE QString currentStateJson() const` returns the latest state snapshot.
- `Q_INVOKABLE void dispatch(const QString& commandJson)` validates and dispatches one command.
- Signals: `stateChanged(QString)`, `commandResult(QString)`, `bridgeError(QString)`.
- Consumes the existing controller properties, invokables, models and signals.

- [ ] **Step 1: Add failing tests for state and command behavior**

Use `QSignalSpy` and a real `GuiChatController` instance. Cover:

```cpp
void ChatBridgeTests::initialSnapshotHasSchemaAndDisconnectedState();
void ChatBridgeTests::snapshotContainsRoomAndMemberRoles();
void ChatBridgeTests::dispatchSelectRoomCallsController();
void ChatBridgeTests::invalidJsonEmitsCommandResultWithoutCallingController();
void ChatBridgeTests::passwordDoesNotAppearInSnapshotOrResult();
void ChatBridgeTests::modelChangesAreCoalescedIntoOneStateChangedSignal();
```

The test must inspect JSON, not private C++ implementation details. It must verify the snapshot contains `connection`, `identity`, `navigation`, `rooms`, `directMessages`, `activeMessages`, `members`, `permissions` and `savedConnection`.

- [ ] **Step 2: Run the tests and verify the expected failures**

Run:

```powershell
cmake --build .\client-cpp\gui\build-bridge --target chat-bridge-tests
ctest --test-dir .\client-cpp\gui\build-bridge -R chat-bridge-tests --output-on-failure
```

Expected: compilation or assertion failure because `ChatBridge` and the structured controller signals are not present.

- [ ] **Step 3: Add only the controller signals required by the adapter**

Add additive signals to `GuiChatController` for connection failure/lost and a generic state-affecting event. Keep existing QML properties and invokables unchanged. Emit them from `handleConnectionFailed`, `handleConnectionLost`, and the existing model/property update paths. Do not move worker logic or add business validation in the bridge.

- [ ] **Step 4: Implement model-to-JSON serialization**

In `chat_bridge.cpp`, read `QAbstractItemModel::roleNames()` and `data()` generically. Serialize only the active model window needed by React; preserve role names and booleans. Serialize the existing `roomModel`, `directMessageModel`, active message model and `memberModel`. Do not read password fields or process-local private-key contents.

- [ ] **Step 5: Implement command dispatch and result signaling**

Parse one UTF-8 JSON document, call `bridge::validateCommand`, dispatch only to the exact `GuiChatController` invokable named in the spec, and emit one result with the input command id. Use queued controller calls where the existing controller uses queued worker calls. A malformed command emits `invalid_command`; a controller/server failure emits the structured controller error without exposing raw secrets.

- [ ] **Step 6: Coalesce state updates**

Connect model signals and controller state signals to a single `QTimer` with a 16ms interval. Each timeout rebuilds one complete snapshot and emits `stateChanged`. On bridge construction, build the initial snapshot. On a web channel reconnect, React will call `currentStateJson()` rather than expecting replay.

- [ ] **Step 7: Run all C++ GUI tests**

Run:

```powershell
cmake --build .\client-cpp\gui\build-bridge --parallel 2
ctest --test-dir .\client-cpp\gui\build-bridge --output-on-failure
```

Expected: bridge tests and existing C++ tests pass; QML remains the default application path.

- [ ] **Step 8: Commit the bridge adapter**

```powershell
git add client-cpp/gui/src/chat_bridge.hpp client-cpp/gui/src/chat_bridge.cpp client-cpp/gui/src/gui_chat_controller.hpp client-cpp/gui/src/gui_chat_controller.cpp client-cpp/gui/tests/chat_bridge_tests.cpp client-cpp/gui/CMakeLists.txt
git commit -m "feat(gui): add ChatBridge state and command seam"
```

### Task 3: Create the React shell and mock bridge contract

**Files:**
- Create: `frontend/package.json`
- Create: `frontend/package-lock.json`
- Create: `frontend/tsconfig.json`
- Create: `frontend/tsconfig.node.json`
- Create: `frontend/vite.config.ts`
- Create: `frontend/vitest.config.ts`
- Create: `frontend/index.html`
- Create: `frontend/src/main.tsx`
- Create: `frontend/src/app/App.tsx`
- Create: `frontend/src/bridge/types.ts`
- Create: `frontend/src/bridge/chatBridge.ts`
- Create: `frontend/src/state/useBridgeState.ts`
- Create: `frontend/src/styles/tokens.css`
- Create: `frontend/src/styles/global.css`
- Create: `frontend/src/app/App.test.tsx`
- Create: `frontend/src/bridge/chatBridge.test.ts`

**Interfaces:**
- `ChatBridgeClient.currentStateJson(): string`.
- `ChatBridgeClient.dispatch(command: BridgeCommand): void`.
- `ChatBridgeClient.subscribe(listener: (state: BridgeState) => void): () => void`.
- The browser adapter resolves `window.qt.webChannelTransport` with QWebChannel; tests inject a fake object with the same three methods.

- [ ] **Step 1: Write failing React tests**

Test that a disconnected snapshot renders the Chinese mode page, that clicking remote connection produces `session.connectRemote`, and that a state update changes the page without reloading the application.

- [ ] **Step 2: Run the frontend tests and verify failure**

Run with the portable runtime:

```powershell
$NodeRoot='C:\Users\jking1\Desktop\my-project\chat_X\.tools\node-v24.19.0-win-x64'
$env:Path="$NodeRoot;$NodeRoot\node_modules\npm\bin;$env:Path"
& "$NodeRoot\node.exe" "$NodeRoot\node_modules\npm\bin\npm-cli.js" test -- --run
```

Expected: npm reports the frontend scripts or source modules are not yet defined.

- [ ] **Step 3: Create the minimal Vite/React project**

Use React and TypeScript with Vite. Add only the specified UI dependencies: Tailwind CSS, Motion and Lucide. Add Vitest, jsdom and React Testing Library as development dependencies. Configure `npm test` to run `vitest run` and `npm run build` to run `tsc --noEmit && vite build`.

- [ ] **Step 4: Implement the bridge client and state hook**

Define the TypeScript forms of `BridgeState`, `BridgeError`, `BridgeCommand`, `CommandResult` and the model summaries from the design spec. Keep the password field only in the connection form component; never copy it into `BridgeState` or log it. The adapter must parse `currentStateJson()` and handle QWebChannel disconnect with an inline error state.

- [ ] **Step 5: Implement the first shell screens**

Implement `App`, `ModeSelectionPage`, and minimal connection form state using Chinese labels and Aurora tokens. Do not connect to real C++ yet; the fake bridge is the test adapter. Use `transform`/`opacity` transitions only.

- [ ] **Step 6: Run frontend tests and production build**

Run:

```powershell
& "$NodeRoot\node.exe" "$NodeRoot\node_modules\npm\bin\npm-cli.js" test -- --run
& "$NodeRoot\node.exe" "$NodeRoot\node_modules\npm\bin\npm-cli.js" run build
```

Expected: tests pass and `frontend/dist/index.html` is generated without an external URL dependency.

- [ ] **Step 7: Commit the React shell**

```powershell
git add frontend
git commit -m "feat(frontend): add React bridge shell"
```

### Task 4: Pass the Qt WebEngine dependency gate

**Files:**
- Modify only the local Qt installation or use an explicitly approved Qt package installation outside the repository.
- Create: `docs/superpowers/plans/2026-08-21-react-webengine-ui-dependency-check.md` only if the installed component paths differ from the plan.

**Interfaces:**
- Produces valid CMake package paths for `Qt6WebEngineQuick` and `Qt6WebChannel` under the selected Qt 6.11 MinGW prefix.
- Produces runnable Qt DLLs for local development and release packaging.

- [ ] **Step 1: Recheck the dependency paths**

Run:

```powershell
Get-ChildItem 'C:\Qt\6.11.2\mingw_64\lib\cmake' -Name | Where-Object { $_ -match 'WebEngine|WebChannel' }
Get-ChildItem 'C:\Qt\6.11.2\mingw_64\bin' -Name | Where-Object { $_ -match 'WebEngine|WebChannel' }
```

Expected after installation: both CMake package families and their runtime DLLs are present.

- [ ] **Step 2: If missing, stop and request the Qt component installation**

Use the Qt Maintenance Tool or an official Qt 6.11 MinGW package with WebEngine and WebChannel selected. Do not substitute Electron, a system WebView, or a random DLL copy. The repository implementation must remain disabled until the selected Qt prefix passes the path check.

- [ ] **Step 3: Verify CMake can resolve the components**

Run:

```powershell
cmake -S .\client-cpp\gui -B .\client-cpp\gui\build-webengine -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_CXX_COMPILER='C:\Qt\Tools\mingw1310_64\bin\g++.exe' `
  -DCMAKE_PREFIX_PATH='C:\Qt\6.11.2\mingw_64' `
  -DLAN_CHAT_ENABLE_WEB_UI=ON
```

Expected: CMake finds `Qt6::WebEngineQuick` and `Qt6::WebChannel`. If it fails, report the exact missing package and leave the QML build unaffected.

### Task 5: Add WebEngine host with QML fallback

**Files:**
- Create: `client-cpp/gui/src/web_ui_host.hpp`
- Create: `client-cpp/gui/src/web_ui_host.cpp`
- Create: `client-cpp/gui/resources/frontend.qrc`
- Modify: `client-cpp/gui/src/main.cpp`
- Modify: `client-cpp/gui/CMakeLists.txt`
- Create: `client-cpp/gui/tests/web_ui_host_tests.cpp`

**Interfaces:**
- `WebUiHost::loadDevelopment(QWebEngineView*, const QUrl&)` accepts only a loopback URL.
- `WebUiHost::loadRelease(QWebEngineView*)` loads `qrc:/frontend/index.html`.
- `WebUiHost::registerBridge(QWebEnginePage*, ChatBridge*)` registers `chatBridge` through `QWebChannel`.
- Existing QML startup remains the default when `LAN_CHAT_ENABLE_WEB_UI=OFF` or when the React host fails before a page is ready.

- [ ] **Step 1: Add failing host tests and CMake option checks**

Test the URL policy as pure functions: loopback HTTP development URL accepted; non-loopback HTTP and arbitrary release URLs rejected; release URL equals `qrc:/frontend/index.html`.

- [ ] **Step 2: Add the optional WebEngine/WebChannel CMake components**

Define `option(LAN_CHAT_ENABLE_WEB_UI "Build the React WebEngine UI" OFF)`. Only inside the ON branch call `find_package(Qt6 6.5 REQUIRED COMPONENTS WebEngineQuick WebChannel)`, link the corresponding Qt targets, add host sources, and add the resource file. Keep the existing QML target link set unchanged when OFF.

- [ ] **Step 3: Implement the host and bridge registration**

Create one `QWebEngineView`, one `QWebChannel`, register `chatBridge`, and set the channel before loading the URL. Development mode is selected only by an explicit command-line flag; release mode uses qrc. A failed React load exits the WebEngine path and starts the existing QML engine.

- [ ] **Step 4: Embed the built frontend dist**

Add only the files under `frontend/dist` to `frontend.qrc`; use a configure-time check that fails with a clear message if `frontend/dist/index.html` is missing. Do not embed Node or the frontend source tree.

- [ ] **Step 5: Build both modes**

Run the existing QML build with `LAN_CHAT_ENABLE_WEB_UI=OFF`, then the WebEngine build with it ON. Expected: QML mode remains runnable; WebEngine mode loads a qrc page and exposes `chatBridge`.

- [ ] **Step 6: Commit the optional host**

```powershell
git add client-cpp/gui/src/web_ui_host.hpp client-cpp/gui/src/web_ui_host.cpp client-cpp/gui/resources/frontend.qrc client-cpp/gui/src/main.cpp client-cpp/gui/CMakeLists.txt client-cpp/gui/tests/web_ui_host_tests.cpp
git commit -m "feat(gui): add optional WebEngine host"
```

### Task 6: Implement the Aurora workspace with a fake bridge first

**Files:**
- Create: `frontend/src/app/WorkspacePage.tsx`
- Create: `frontend/src/components/WorkspaceRail.tsx`
- Create: `frontend/src/components/ConversationSidebar.tsx`
- Create: `frontend/src/components/ChatHeader.tsx`
- Create: `frontend/src/components/MessageTimeline.tsx`
- Create: `frontend/src/components/MessageItem.tsx`
- Create: `frontend/src/components/MessageComposer.tsx`
- Create: `frontend/src/components/MemberPanel.tsx`
- Create: `frontend/src/components/IdentityCard.tsx`
- Create: `frontend/src/components/ModalSurface.tsx`
- Create: `frontend/src/app/WorkspacePage.test.tsx`
- Modify: `frontend/src/styles/global.css`

**Interfaces:**
- Components consume `BridgeState` and emit typed `BridgeCommand` values through `ChatBridgeClient`.
- `MessageItem` receives `MessageItem` plus `onCopy`, `onQuote`, `onLocalDelete`, `onRecall` callbacks.
- `MessageTimeline` uses a virtualized rendering window when the active message count exceeds 500.

- [ ] **Step 1: Add failing workspace tests**

Cover the four-region layout, fixed bottom-left identity card, self-message right alignment, long-message wrapping, room/DM isolation, unread reset command and member drawer behavior.

- [ ] **Step 2: Implement the workspace structure**

Use CSS grid for the rail/sidebar/chat/member regions. At compact width, replace the member region with `ModalSurface`. Keep the identity card outside the message scroll container.

- [ ] **Step 3: Implement the message cluster alignment**

Render self messages as one right-aligned cluster containing metadata and bubble. Set `overflow-wrap: anywhere` on content, constrain bubble width, and keep metadata in the same flex column. Do not apply backdrop blur inside `MessageTimeline` or `MessageItem`.

- [ ] **Step 4: Implement room, private message and member actions**

Use the typed bridge client for `conversation.selectRoom`, `conversation.selectDirect`, `chat.sendRoom`, `chat.sendPrivate`, `directory.refreshUsers`, `directory.refreshRooms`, `conversation.openPrivate` and the message actions. Keep the composer content when a command result is unsuccessful.

- [ ] **Step 5: Add keyboard, contrast and reduced-motion behavior**

Add focus-visible styles, Chinese labels, Escape-to-close modals, Enter-to-send, a reduced-motion media query and status text alongside status colors.

- [ ] **Step 6: Run frontend tests and build**

```powershell
& "$NodeRoot\node.exe" "$NodeRoot\node_modules\npm\bin\npm-cli.js" test -- --run
& "$NodeRoot\node.exe" "$NodeRoot\node_modules\npm\bin\npm-cli.js" run build
```

Expected: workspace tests pass, generated dist remains suitable for qrc embedding, and no message item uses runtime blur.

- [ ] **Step 7: Commit the workspace**

```powershell
git add frontend/src
git commit -m "feat(frontend): add Aurora chat workspace"
```

### Task 7: Connect real bridge commands and complete the migration slice

**Files:**
- Modify: `frontend/src/bridge/chatBridge.ts`
- Modify: `frontend/src/app/App.tsx`
- Modify: `frontend/src/app/WorkspacePage.tsx`
- Modify: `client-cpp/gui/src/chat_bridge.cpp`
- Modify: `client-cpp/gui/src/gui_chat_controller.cpp`
- Modify: `docs/testing.md`
- Create: `docs/react-webengine-testing.md`

**Interfaces:**
- Real WebChannel and fake bridge implement the same `ChatBridgeClient` interface.
- The React app can hydrate from `currentStateJson()` after page load or channel reconnect.

- [ ] **Step 1: Add integration fixtures for connected, error and reconnect states**

Use the same JSON snapshots in C++ and TypeScript tests. Include a connected Alice identity, lobby/Bob entries, one self message, one remote message, an admin permission, a connection failure and a web channel disconnect.

- [ ] **Step 2: Replace fake bridge wiring with QWebChannel discovery**

Wait for `qt.webChannelTransport`, instantiate `QWebChannel`, require `objects.chatBridge`, and reject any page that lacks `currentStateJson`, `dispatch`, or `stateChanged`. Keep the fake adapter available to unit tests.

- [ ] **Step 3: Wire connection and workspace commands**

Verify each command id produces one `commandResult`, each state mutation produces a coalesced `stateChanged`, and the UI does not optimistically claim server success. Keep password handling in the connection form only.

- [ ] **Step 4: Add C++/React integration checks**

Build the WebEngine target, run CTest, run frontend tests/build, and launch the GUI in QML mode and React mode. Record exact commands and expected results in `docs/react-webengine-testing.md`.

- [ ] **Step 5: Run real localhost TLS two-client acceptance**

Use the existing Go server and two client identities to verify login, Chinese room chat, private-message isolation, offline delivery, channel permissions, admin recall, disconnect/reconnect and CA validation. Clean processes and port 8888 afterward.

- [ ] **Step 6: Run LAN acceptance before any QML removal**

Use the server's real LAN IPv4 and `Test-NetConnection <server-ip> -Port 8888`; do not use `0.0.0.0` as a client destination. Record physical two-client evidence separately from localhost evidence.

- [ ] **Step 7: Commit the integrated migration slice**

```powershell
git add frontend client-cpp/gui docs/testing.md docs/react-webengine-testing.md
git commit -m "feat(gui): connect React UI through ChatBridge"
```

### Task 8: Release verification and QML fallback decision

**Files:**
- Modify: `scripts/package-release.ps1`
- Modify: `docs/release-setup.md`
- Modify: `docs/CODEX_HANDOFF.md`
- Create: `docs/react-webengine-release-checklist.md`

**Interfaces:**
- Release packaging embeds `frontend/dist` and deploys required Qt WebEngine/WebChannel DLLs.
- QML remains an explicit fallback until the checklist records successful real TLS two-client acceptance.

- [ ] **Step 1: Add release asset checks**

Make the package script verify `frontend/dist/index.html`, the qrc resource, Qt WebEngine runtime files, Qt WebChannel runtime files, `platforms/qwindows.dll`, image formats, OpenSSL DLLs and the existing server/certificate layout. Fail with the missing absolute path.

- [ ] **Step 2: Build a clean release package**

Run `scripts/package-release.ps1` from the primary checkout and inspect the generated directory on a clean Windows target. Confirm no Node runtime is required.

- [ ] **Step 3: Run the release two-client checklist**

Verify mode selection, TLS connection, chat, private messages, channels, admin permissions, reconnect, Chinese rendering, message virtualization and QML fallback. Capture failures as evidence, not as silent retries.

- [ ] **Step 4: Update handoff status**

Only after all acceptance rows pass, update `docs/CODEX_HANDOFF.md` to identify the React UI as verified. Until then, state that the migration is partial and QML remains the supported fallback.

- [ ] **Step 5: Commit release verification documentation**

```powershell
git add scripts/package-release.ps1 docs/release-setup.md docs/CODEX_HANDOFF.md docs/react-webengine-release-checklist.md
git commit -m "docs: verify React WebEngine release path"
```

