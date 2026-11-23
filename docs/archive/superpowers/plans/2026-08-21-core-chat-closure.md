# 核心聊天闭环 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the React workspace's core room, direct-message, quote, message-action, and command-feedback flows using the existing ChatBridge commands.

**Architecture:** Keep all network behaviour behind the existing `React -> QWebChannel -> ChatBridge -> GuiChatController -> GuiConnectionWorker -> Go Server` seam. Add workspace-level UI state for modal forms, composer drafts, and command feedback; reuse commands already accepted by `client-cpp/gui/src/chat_bridge.cpp` and do not change the Go server or C++ protocol layer.

**Tech Stack:** React, TypeScript, Vitest, Testing Library, Qt 6 WebEngine, QWebChannel, C++20/Qt Test, Vite.

**Spec:** `docs/superpowers/specs/2026-08-21-core-chat-closure-design.md`

## Global Constraints

- Preserve the Go TLS server, 4-byte big-endian framing, JSON protocol, C++ network worker, and existing ChatBridge command names.
- React must not handle sockets, TLS, passwords, certificates, threads, or database state.
- All logged-in users may create rooms; room owners and administrators manage rooms; other users join, send, and leave only.
- `Remove` hides only the local message; `Recall` is available only to the author or an administrator and uses the existing recall command.
- `Quote` prepends editable plain text and must not add protocol fields or reply-thread persistence.
- Keep command failures visible and retain the affected form/composer input for retry.

---

## File Structure

- `frontend/src/app/WorkspacePage.tsx`: owns quote draft, create-room dialog state, command feedback, and component wiring.
- `frontend/src/components/ConversationSidebar.tsx`: exposes create-room entry and room creation command submission.
- `frontend/src/components/MemberPanel.tsx`: exposes direct-message entry for non-local members.
- `frontend/src/components/MessageComposer.tsx`: supports a controlled draft and reports send command feedback.
- `frontend/src/components/MessageItem.tsx`: reports quote requests and applies author/admin recall visibility.
- `frontend/src/components/CommandFeedback.tsx`: renders a small pending/success/error status region.
- `frontend/src/app/WorkspacePage.test.tsx`, `frontend/src/components/MessageComposer.test.tsx`, `frontend/src/components/MessageItem.test.tsx`: consumer-visible UI and command regression tests.
- `frontend/src/styles/global.css`: styles dialog fields, feedback, member actions, and disabled controls.

### Task 1: Add command-feedback and controlled composer seam

**Files:**
- Create: `frontend/src/components/CommandFeedback.tsx`
- Create: `frontend/src/components/MessageComposer.test.tsx`
- Modify: `frontend/src/components/MessageComposer.tsx`
- Modify: `frontend/src/app/WorkspacePage.tsx`
- Modify: `frontend/src/styles/global.css`

**Interfaces:**
- Consumes: `ChatBridgeClient.subscribeCommandResult`, `BridgeCommand`, and the active conversation from `BridgeState`.
- Produces: `MessageComposerProps` with `draft: string`, `onDraftChange(value: string): void`, and `onCommandResult(result: CommandResult): void`; `CommandFeedbackProps` with `status: 'idle' | 'pending' | 'success' | 'error'` and `message: string`.

- [ ] **Step 1: Write the failing composer tests**

```tsx
it('keeps a failed send draft and reports the error', () => {
  const bridge = createFakeBridge(connectedRoomState);
  render(<MessageComposer state={connectedRoomState} bridge={bridge} draft="retry" onDraftChange={vi.fn()} onCommandResult={vi.fn()} />);
  fireEvent.click(screen.getByRole('button', { name: 'send message' }));
  bridge.publishCommandResult({ id: bridge.commands[0].id, ok: false, error: rejectedError });
  expect(screen.getByLabelText('message composer')).toHaveValue('retry');
  expect(screen.getByRole('alert')).toHaveTextContent('rejected');
});
```

- [ ] **Step 2: Run the targeted test and verify it fails**

Run: `npm test -- --run src/components/MessageComposer.test.tsx`

Expected: FAIL because `MessageComposer` does not accept controlled draft/feedback props and no alert exists.

- [ ] **Step 3: Implement the smallest controlled-composer and feedback flow**

```tsx
type MessageComposerProps = {
  state: BridgeState; bridge: ChatBridgeClient; draft: string;
  onDraftChange(value: string): void;
  onCommandResult(result: CommandResult): void;
};

<input value={draft} onChange={(event) => onDraftChange(event.target.value)} />
```

Subscribe only to the submitted command ID. On success clear via `onDraftChange('')`; on failure leave the draft untouched and pass the result to `onCommandResult`.

- [ ] **Step 4: Run the targeted test and verify it passes**

Run: `npm test -- --run src/components/MessageComposer.test.tsx`

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add frontend/src/components/CommandFeedback.tsx frontend/src/components/MessageComposer.tsx frontend/src/components/MessageComposer.test.tsx frontend/src/app/WorkspacePage.tsx frontend/src/styles/global.css
git commit -m "feat: add chat command feedback"
```

### Task 2: Create public/private rooms from the sidebar

**Files:**
- Modify: `frontend/src/app/WorkspacePage.tsx`
- Modify: `frontend/src/components/ConversationSidebar.tsx`
- Modify: `frontend/src/app/WorkspacePage.test.tsx`
- Modify: `frontend/src/styles/global.css`

**Interfaces:**
- Consumes: existing `room.create` and `directory.refreshRooms` commands.
- Produces: `ConversationSidebarProps` additions `onCreateRoom(): void`; a workspace dialog with `roomName`, `isPrivate`, and submit command state.

- [ ] **Step 1: Write the failing room-creation tests**

```tsx
it('creates a private room then refreshes rooms on success', () => {
  const bridge = createFakeBridge(workspaceState);
  render(<WorkspacePage bridge={bridge} state={workspaceState} />);
  fireEvent.click(screen.getByRole('button', { name: 'create room' }));
  fireEvent.change(screen.getByLabelText('room name'), { target: { value: 'study' } });
  fireEvent.click(screen.getByLabelText('private room'));
  fireEvent.click(screen.getByRole('button', { name: 'confirm create room' }));
  expect(bridge.commands[0]).toMatchObject({ type: 'room.create', payload: { room: 'study', isPrivate: true } });
});
```

- [ ] **Step 2: Run the targeted test and verify it fails**

Run: `npm test -- --run src/app/WorkspacePage.test.tsx`

Expected: FAIL because no create-room entry or dialog exists.

- [ ] **Step 3: Implement the create-room dialog and command result handling**

```tsx
bridge.dispatch(createCommand('room.create', { room: roomName.trim(), isPrivate }));
```

Disable empty submissions and duplicate submits. When the matching result succeeds, dispatch `directory.refreshRooms`, close and reset the dialog; when it fails, retain values and show the error through `CommandFeedback`.

- [ ] **Step 4: Run the targeted test and verify it passes**

Run: `npm test -- --run src/app/WorkspacePage.test.tsx`

Expected: PASS, including the existing workspace tests.

- [ ] **Step 5: Commit**

```powershell
git add frontend/src/app/WorkspacePage.tsx frontend/src/app/WorkspacePage.test.tsx frontend/src/components/ConversationSidebar.tsx frontend/src/styles/global.css
git commit -m "feat: add room creation flow"
```

### Task 3: Open direct chats from members and quote messages

**Files:**
- Modify: `frontend/src/app/WorkspacePage.tsx`
- Modify: `frontend/src/components/MemberPanel.tsx`
- Modify: `frontend/src/components/MessageItem.tsx`
- Modify: `frontend/src/components/MessageItem.test.tsx`
- Modify: `frontend/src/app/WorkspacePage.test.tsx`
- Modify: `frontend/src/styles/global.css`

**Interfaces:**
- Consumes: existing `conversation.openPrivate` command and `MessageItemData` fields.
- Produces: `MemberPanelProps` additions `localUserCode: string` and `onStartDirect(member: MemberSummary): void`; `MessageItemProps` addition `onQuote(message: MessageItemData): void` wired by `WorkspacePage`.

- [ ] **Step 1: Write failing direct-message and quote tests**

```tsx
it('opens a private conversation for another member but not the local member', () => {
  render(<MemberPanel state={workspaceState} localUserCode="A001" onStartDirect={onStartDirect} />);
  expect(screen.queryByRole('button', { name: 'direct-Alice' })).not.toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'direct-Bob' }));
  expect(onStartDirect).toHaveBeenCalledWith(expect.objectContaining({ userCode: 'B002' }));
});

it('prefills an editable quote in the workspace composer', () => {
  render(<WorkspacePage bridge={bridge} state={workspaceState} />);
  fireEvent.click(screen.getByRole('button', { name: 'Quote' }));
  expect(screen.getByLabelText('message composer')).toHaveValue('> Bob: A very long message');
});
```

- [ ] **Step 2: Run the targeted tests and verify they fail**

Run: `npm test -- --run src/app/WorkspacePage.test.tsx src/components/MessageItem.test.tsx`

Expected: FAIL because member direct actions are absent and quote is not wired to the composer draft.

- [ ] **Step 3: Implement member direct actions and quote draft formatting**

```tsx
bridge.dispatch(createCommand('conversation.openPrivate', {
  displayName: member.displayName,
  userCode: member.userCode
}));

setDraft(`> ${message.displayName}: ${message.content}\n`);
```

Do not render a direct-chat button for `member.userCode === state.identity.userCode`. Preserve any existing composer text by prefixing the quote and a newline.

- [ ] **Step 4: Run the targeted tests and verify they pass**

Run: `npm test -- --run src/app/WorkspacePage.test.tsx src/components/MessageItem.test.tsx`

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add frontend/src/app/WorkspacePage.tsx frontend/src/app/WorkspacePage.test.tsx frontend/src/components/MemberPanel.tsx frontend/src/components/MessageItem.tsx frontend/src/components/MessageItem.test.tsx frontend/src/styles/global.css
git commit -m "feat: add direct chat and quote actions"
```

### Task 4: Gate message removal and recall by local permissions

**Files:**
- Modify: `frontend/src/components/MessageItem.tsx`
- Modify: `frontend/src/components/MessageItem.test.tsx`
- Modify: `frontend/src/app/WorkspacePage.tsx`

**Interfaces:**
- Consumes: `state.identity.userCode`, `state.identity.admin`, and existing `message.removeLocal` / `message.recall` commands.
- Produces: `MessageItemProps` additions `canRecall: boolean` and command dispatch for the existing action names.

- [ ] **Step 1: Write failing permission tests**

```tsx
it('shows Recall only to the message author or an administrator', () => {
  render(<MessageItem message={peerMessage} bridge={bridge} canRecall={false} />);
  expect(screen.queryByRole('button', { name: 'Recall' })).not.toBeInTheDocument();
  rerender(<MessageItem message={peerMessage} bridge={bridge} canRecall />);
  expect(screen.getByRole('button', { name: 'Recall' })).toBeInTheDocument();
});
```

- [ ] **Step 2: Run the targeted test and verify it fails**

Run: `npm test -- --run src/components/MessageItem.test.tsx`

Expected: FAIL because Recall is currently tied only to `selfMessage` and has no permission prop.

- [ ] **Step 3: Implement the permission gate and preserve Remove semantics**

```tsx
{message.selfMessage && <button onClick={removeLocal}>Remove</button>}
{canRecall && <button onClick={recall}>Recall</button>}
```

Compute `canRecall` in `WorkspacePage` as `state.identity.admin || message.userCode === state.identity.userCode`. Keep `Remove` client-local and do not alter the C++ bridge commands.

- [ ] **Step 4: Run the targeted test and verify it passes**

Run: `npm test -- --run src/components/MessageItem.test.tsx`

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add frontend/src/app/WorkspacePage.tsx frontend/src/components/MessageItem.tsx frontend/src/components/MessageItem.test.tsx
git commit -m "feat: enforce message recall permissions"
```

### Task 5: Run end-to-end build verification and manual acceptance

**Files:**
- Modify: `client-cpp/gui/resources/frontend.qrc` only if Vite's asset hashes change.

**Interfaces:**
- Consumes: the final frontend `dist/index.html` asset names and existing CMake `lan-chat-gui` target.
- Produces: a GUI executable embedding the matching frontend release bundle.

- [ ] **Step 1: Run all frontend tests**

Run: `C:\Users\jking1\Desktop\my-project\chat_X\.tools\node-v24.19.0-win-x64\npm.cmd test -- --run`

Expected: all Vitest tests pass.

- [ ] **Step 2: Build the frontend and synchronize Qt resource aliases**

Run: `C:\Users\jking1\Desktop\my-project\chat_X\.tools\node-v24.19.0-win-x64\npm.cmd run build`

Read `frontend/dist/index.html`, update the two hashed asset aliases in `client-cpp/gui/resources/frontend.qrc`, and verify each qrc path exists.

- [ ] **Step 3: Build and run C++ bridge tests in the MSVC environment**

Run:

```powershell
cmd.exe /d /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build . --target lan-chat-gui chat-bridge-tests && set PATH=C:\Qt\6.11.2\msvc2022_64\bin;C:\Users\jking1\AppData\Local\Temp\lan-chat-vcpkg\vcpkg_installed\vcpkg\pkgs\openssl_x64-windows\bin;%PATH% && .\chat-bridge-tests.exe'
```

Run from `client-cpp/gui/build-webengine-msvc-ninja2`. Expected: build exit code 0 and `chat-bridge-tests.exe` exit code 0.

- [ ] **Step 4: Perform manual two-user acceptance**

Start the Go server in one terminal and two GUI clients in separate terminals. Verify room creation, direct messages, quote prefill, local Remove, author/admin Recall, and a deliberately rejected command whose input remains available to retry.

- [ ] **Step 5: Commit**

```powershell
git add frontend client-cpp/gui/resources/frontend.qrc
git commit -m "feat: complete core chat interactions"
```

## Plan Self-Review

- Spec coverage: Tasks 1-4 cover command feedback, room creation, direct chat, quote, local removal, recall permissions, and retained inputs. Task 5 covers frontend, C++ bridge, bundled resource, and two-user acceptance verification.
- Placeholder scan: every task has explicit files, commands, expected outcomes, and implementation detail.
- Type consistency: all planned command names already exist in `ChatBridge`; component callback names are defined in the task that introduces them.
