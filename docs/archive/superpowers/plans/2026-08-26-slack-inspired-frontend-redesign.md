# LAN Chat 雾蓝工作台前端改版 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将全部 React/WebEngine 页面改为雾蓝与石墨的局域网聊天工作台，同时保持现有聊天、连接、权限和桥接行为不变。

**Architecture:** 复用现有页面、组件和 `ChatBridge` 契约，只调整语义属性、页面文案和 CSS 视觉层。`global.css` 以一组雾蓝 token 替换末尾的奶白纸张覆盖层；现有 class 名、组件 props、命令类型和移动端 modal 流程继续作为稳定接口。

**Tech Stack:** React 19、TypeScript、Vitest、Testing Library、CSS Grid、现有 `lucide-react` 与 Qt WebEngine 打包流程。

## Global Constraints

- 只修改 `frontend/`；不得修改 Go、C++、Qt/QML、ChatBridge、安装器、数据目录或协议。
- 不新增 npm runtime 依赖、主题切换、线程、搜索、文件传输或账号功能。
- 主色为 `#2D6176`，活动色为 `#3A778C`，导航表面为 `#E5EEF3/#F0F6F8`，内容表面为白色，正文为 `#1E2D33`。
- 保留现有中文 aria 标签和交互；不依赖颜色单独表达错误、在线、未读或禁用状态。
- 760px 以下继续复用已有会话/成员 modal 入口；不新建移动端路由或 drawer state。
- 动效只允许 `opacity`/`transform`，不超过 160ms，并保持 `prefers-reduced-motion` 降级。
- 不读取、记录或渲染密码、私钥、证书内容、数据库内容或聊天内容之外的敏感数据。

---

### Task 1: 锁定工作区导航的可访问性状态

**Files:**
- Modify: `frontend/src/components/WorkspaceRail.tsx`
- Modify: `frontend/src/components/ConversationSidebar.tsx`
- Modify: `frontend/src/components/WorkspaceRail.test.tsx`
- Modify: `frontend/src/app/WorkspacePage.test.tsx`

**Interfaces:**
- Consumes: `WorkspaceRailProps.section: 'rooms' | 'direct'` 与 `BridgeState.navigation.activeConversation`。
- Produces: 群聊/私信切换按钮的 `aria-pressed`，活动会话按钮的 `aria-current="page"`；不改变任何 `onSectionChange` 或 `bridge.dispatch` payload。

- [ ] **Step 1: 为当前导航状态写失败测试**

在 `frontend/src/components/WorkspaceRail.test.tsx` 加入：

```tsx
it('exposes the active workspace section to assistive technology', () => {
  render(<WorkspaceRail section="rooms" onSectionChange={vi.fn()} />);

  expect(screen.getByRole('button', { name: '群' })).toHaveAttribute('aria-pressed', 'true');
  expect(screen.getByRole('button', { name: '私' })).toHaveAttribute('aria-pressed', 'false');
});
```

在 `frontend/src/app/WorkspacePage.test.tsx` 的现有“separates 群 and 私 navigation lists”测试后加入：

```tsx
expect(screen.getByRole('button', { name: 'room-lobby' })).toHaveAttribute('aria-current', 'page');
```

- [ ] **Step 2: 运行测试并确认失败原因是缺少语义属性**

Run: `pnpm test -- --run src/components/WorkspaceRail.test.tsx src/app/WorkspacePage.test.tsx`

Expected: 新增断言失败，提示缺少 `aria-pressed` 或 `aria-current`；既有命令 payload 测试仍通过。

- [ ] **Step 3: 添加最小语义属性**

在 `WorkspaceRail.tsx` 的两个切换按钮分别加入：

```tsx
aria-pressed={section === 'rooms'}
```

和：

```tsx
aria-pressed={section === 'direct'}
```

在 `ConversationSidebar.tsx` 的两个会话按钮中加入对应条件属性：

```tsx
aria-current={active?.kind === 'room' && active.id === room.roomName ? 'page' : undefined}
```

```tsx
aria-current={active?.kind === 'dm' && active.userCode === direct.userCode ? 'page' : undefined}
```

同时将 `App.tsx` 中模式页的 eyebrow 文案从 `LAN CHAT / AURORA GLASS` 改为 `LAN CHAT / LOCAL WORKSPACE`，不改按钮名称或模式回调。

- [ ] **Step 4: 运行针对性测试确认通过**

Run: `pnpm test -- --run src/components/WorkspaceRail.test.tsx src/app/WorkspacePage.test.tsx src/app/App.test.tsx`

Expected: 以上测试文件全部通过；切换群聊/私信、选择会话、打开设置和发送 bridge 命令的既有断言不变。

- [ ] **Step 5: 提交语义基线**

```powershell
git add -- frontend/src/components/WorkspaceRail.tsx frontend/src/components/ConversationSidebar.tsx frontend/src/components/WorkspaceRail.test.tsx frontend/src/app/WorkspacePage.test.tsx frontend/src/app/App.tsx
git commit -m "feat(frontend): expose workspace navigation state"
```

### Task 2: 建立雾蓝共享视觉 token 与表单页面

**Files:**
- Modify: `frontend/src/styles/global.css`
- Test: `frontend/src/app/App.test.tsx`（既有模式入口回归）
- Test: `frontend/src/app/BridgeUnavailablePage.test.tsx`（既有错误恢复回归）

**Interfaces:**
- Consumes: 现有 `.app-shell`、`.mode-panel`、`.connect-panel`、`.settings-panel`、`.modal-surface`、`.primary-button`、`.secondary-button` 和 `.status-line` class。
- Produces: 所有连接、建房、设置、异常和 modal 表面共用的 CSS 自定义属性及雾蓝状态样式；不改变 React props 或 bridge 状态。

- [ ] **Step 1: 运行现有行为回归作为样式改动基线**

Run: `pnpm test -- --run src/app/App.test.tsx src/app/BridgeUnavailablePage.test.tsx`

Expected: 模式入口、bridge 错误 `role="alert"` 和“重试连接”操作均通过；纯 CSS 改动不应制造无意义的测试专用 DOM。

- [ ] **Step 2: 替换纸张主题覆盖层为共享雾蓝 token**

删除 `global.css` 末尾以 `/* Paper chat:` 开始的覆盖规则，替换为以下 token 开头和对应 class 覆盖。保留其前方的布局、滚动、reduced-motion 与组件规则。

```css
:root,
:root[data-theme="dark"],
:root[data-theme="light"] {
  --lc-ink: #1e2d33;
  --lc-muted: #61737b;
  --lc-blue: #2d6176;
  --lc-blue-active: #3a778c;
  --lc-nav: #e5eef3;
  --lc-nav-soft: #f0f6f8;
  --lc-canvas: #f2f6f7;
  --lc-surface: #ffffff;
  --lc-border: #d6e1e5;
  --lc-success: #2e8b70;
  --lc-warning: #b87925;
  --lc-danger: #b64d40;
  color: var(--lc-ink);
  background: var(--lc-canvas);
}

.app-shell { background: var(--lc-canvas); }
.mode-panel, .connect-panel, .settings-panel, .modal-surface, .emoji-picker {
  color: var(--lc-ink);
  background: var(--lc-surface);
  border-color: var(--lc-border);
  box-shadow: 0 18px 42px rgba(32, 67, 79, .10);
}
.primary-button { color: #fff; background: var(--lc-blue); }
.primary-button:hover:not(:disabled) { background: var(--lc-blue-active); }
.secondary-button { color: var(--lc-blue); border-color: #b8c9ce; background: #fff; }
.status-line--error, .command-feedback--error, .room-form__hint { color: var(--lc-danger); }
```

继续为 `.form-grid input`、`.conversation-search`、`.history-search`、`.room-form__field input` 设置白底、`#BCCCD2` 边框与雾蓝 focus ring；为 `.lan-discovery__notice` 设 `#E7F2F5` 背景和左侧 `#4B8DA3` 边线；为 `.status-dot`、`.presence-dot--online` 和 `.identity-status` 使用 `--lc-success`。删除旧的琥珀色 hover 下划线规则，使用边框/背景/文字的可见 hover 与焦点状态。

- [ ] **Step 3: 运行页面测试与生产构建**

Run: `pnpm test -- --run src/app/App.test.tsx src/app/BridgeUnavailablePage.test.tsx && pnpm build`

Expected: 测试通过，TypeScript 无错误，Vite 生成 `dist/`；不产生新依赖。

- [ ] **Step 4: 提交共享表面改版**

```powershell
git add -- frontend/src/styles/global.css frontend/src/app/App.test.tsx frontend/src/app/BridgeUnavailablePage.test.tsx
git commit -m "feat(frontend): add slate blue page system"
```

### Task 3: 完成工作区的雾蓝四栏阅读层级

**Files:**
- Modify: `frontend/src/styles/global.css`
- Test: `frontend/src/app/WorkspacePage.test.tsx`（既有工作区交互回归）
- Test: `frontend/src/components/MessageTimeline.test.tsx`（既有消息列表回归）

**Interfaces:**
- Consumes: 现有 `.workspace-shell` 四区 grid、`.workspace-rail`、`.conversation-sidebar`、`.chat-region`、`.member-panel`、`.message-timeline` 和 `.message-composer` class。
- Produces: 桌面雾蓝导航、白色消息主区、轻量成员栏及原有 760px modal 降级；不改变消息/成员/房间数据或任何 dispatch。

- [ ] **Step 1: 运行工作区交互基线**

Run: `pnpm test -- --run src/app/WorkspacePage.test.tsx src/components/MessageTimeline.test.tsx`

Expected: 四栏区域、活动会话语义、消息引用、成员 modal、频道/管理员命令和空消息状态均通过；该任务只更改视觉 CSS。

- [ ] **Step 2: 重写工作区相关 CSS 覆盖规则**

在 Task 2 的 token 后添加以下结构化样式，并删除旧纸张主题针对相同 selector 的覆盖：

```css
.workspace-shell { grid-template-columns: 60px 232px minmax(0, 1fr) 224px; background: var(--lc-canvas); }
.workspace-rail { color: #234957; background: var(--lc-nav); border-color: var(--lc-border); }
.conversation-sidebar { color: #233f4d; background: var(--lc-nav-soft); border-color: var(--lc-border); }
.chat-region { background: var(--lc-surface); }
.member-panel { color: var(--lc-ink); background: #fbfcfc; border-color: var(--lc-border); }
.conversation-item--active { color: #fff; background: var(--lc-blue-active); }
.conversation-item--active .conversation-copy small { color: #e8f4f7; }
.message-bubble { color: var(--lc-ink); background: #fff; border-color: #dbe5e8; border-radius: 8px; }
.message--self .message-bubble { color: #16343f; background: #e7f2f5; border-color: #c4dce3; }
.message-composer { background: #fff; border-color: #b8c9ce; border-radius: 8px; }
.send-button { color: #fff; background: var(--lc-blue); border-radius: 6px; }
```

保留消息的 `.message--self`、`.message-content--wrap`、引用、撤回、复制、未读、滚动到底部和 composer quote 选择器；只改颜色、间距、边框、圆角和 hover。760px media query 必须继续 `display:none` 静态 sidebar/member panel，并允许它们在 `.modal-surface` 内显示。

- [ ] **Step 3: 运行全量前端测试与构建**

Run: `pnpm test -- --run && pnpm build`

Expected: 全部前端测试通过，构建通过；频道选择、私信选择、消息引用、成员弹窗、审批、设置与窄窗口入口均无回归。

- [ ] **Step 4: 手动验收可访问性与响应式**

Run: `pnpm dev`

Expected: 在浏览器/Qt WebEngine 中检查 mode、远程连接、局域网发现、建房、workspace、settings 和 bridge-error；在宽度 1280px 和 760px 下确认焦点环、错误/成功文字、活动会话、成员/导航 modal、长消息断行与 reduced-motion 无误。

- [ ] **Step 5: 提交工作区改版**

```powershell
git add -- frontend/src/styles/global.css frontend/src/app/WorkspacePage.test.tsx frontend/src/components/MessageTimeline.test.tsx
git commit -m "feat(frontend): restyle workspace for slate blue chat"
```

### Task 4: Qt WebEngine 集成回归

**Files:**
- Inspect: `frontend/dist/`
- Execute: `scripts/build-modern.ps1`
- Test: Qt CTest suite

**Interfaces:**
- Consumes: 由 `pnpm build` 生成的前端 `dist/` 和现有 Qt resource embedding。
- Produces: 可打包的 WebEngine 前端与通过的 CTest 结果；不修改 C++ 源码。

- [ ] **Step 1: 运行完整现代构建与 CTest**

Run:

```powershell
$qt = (Resolve-Path .\.tools\qt\6.10.3\msvc2022_64).Path
$openssl = (Resolve-Path .\.tools\vcpkg\installed\x64-windows).Path
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Test -QtPrefix $qt -OpenSslRoot $openssl
```

Expected: 前端产物嵌入完成，Qt/C++ 构建成功，CTest 15/15 通过。

- [ ] **Step 2: 检查变更边界**

Run: `git diff --check && git status --short`

Expected: 无空白错误；仅有计划内前端文件和生成的忽略项，原有未跟踪 `LANChat-Launcher.exe` 不被修改或删除。

- [ ] **Step 3: 提交验收记录（仅在需要记录时）**

若实现过程中新增了测试/文档，将它们与对应任务一同提交；本任务不为纯验证制造空提交。
