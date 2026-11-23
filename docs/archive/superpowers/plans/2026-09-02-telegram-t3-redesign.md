# Telegram T3 前端重设计 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 按 `docs/superpowers/specs/2026-09-02-telegram-t3-redesign-design.md` 把 `frontend/` 全量换装 Telegram Air（T3）风格——双栏布局、`--t-*` 单一令牌层、成员覆盖抽屉、✓✓ 回执、六个真实加载态，并按 Codex 定稿契约（`attachment.chooseUpload` / `attachment.event`）接入附件上传预览卡。

**Architecture:** 全部改动限于 `frontend/`。先扩展 bridge 的事件管道（`CommandResult` 通道上分流 `attachment.event`）与纯函数 reducer（TDD），再落实动效双门（`data-effects` + `prefers-reduced-motion`），然后把 829 行三层旧 CSS 替换为 `--t-*` 单一令牌层（拆为 tokens/pages/workspace 三个 partial，`global.css` 变 import hub——对规格 §9 的唯一结构偏离），最后逐组件重排 JSX（rail 并入侧栏、成员抽屉、T3 气泡、附件卡）。

**Tech Stack:** pnpm workspace（`frontend/`）；React 19 + TypeScript + Vite；vitest 4 + @testing-library/react + jsdom；gsap 3（motion.ts）；无新增依赖。

---

## 前置须知（每个任务开工前必读）

1. **工作目录**：所有 pnpm/vitest 命令在 `frontend/` 下执行（`cd frontend && ...`）；git 命令在仓库根执行。
2. **并发协作**：Codex 正在 `client-cpp/`、`server-go/` 上开发附件后端，且**其 QML 删除已暂存在 git index 里**。因此：
   - **禁止** `git add -A` / `git add .`。
   - 每次提交都必须用 pathspec 形式：`git add <本任务的文件> && git commit -m "..." -- <本任务的文件>`。提交前先 `git status --porcelain` 核对本任务文件清单，提交后用 `git show --stat HEAD` 确认只含本任务文件。
   - 绝不 `git reset` / `git stash` / `checkout .`。
3. **常用命令**（均在 `frontend/` 下）：
   - 单测一个文件：`pnpm exec vitest run src/bridge/attachmentEvents.test.ts`
   - 全量测试：`pnpm exec vitest run`
   - 类型检查：`pnpm exec tsc --noEmit`
   - 构建：`pnpm build`（= `tsc --noEmit && vite build`）
   - 预期输出：`Test Files  N passed (N)`、`tsc` 无输出退出码 0。测试失败时先修后行——**每个任务的测试必须全绿才能提交**。
4. **规格 §11 待对齐项的处理原则**：不发明命令/事件。缺失的信息一律用可选字段（`mls?`、`logicalSize?`）+ 降级 UI + 失败兜底文案，联调阻塞点已记录在规格 §11。

---

### Task 1: bridge 附件事件类型与事件分流管道

**Files:**
- Modify: `frontend/src/bridge/types.ts`
- Modify: `frontend/src/bridge/chatBridge.ts`
- Modify: `frontend/src/components/MessageComposer.test.tsx`（ControllableBridge 实现新接口方法）
- Test: `frontend/src/bridge/chatBridge.test.ts`

- [ ] **Step 1: 写失败测试（chatBridge.test.ts 追加两个用例）**

在 `frontend/src/bridge/chatBridge.test.ts` 文件末尾（`describe('createQtBridge')` 块内、`});` 之前）追加：

```ts
  it('routes attachment events to the attachment subscription, not command results', async () => {
    let resultListener: ((json: string) => void) | undefined;
    const proxy = {
      currentStateJson: (callback: (json: string) => void) => callback(JSON.stringify(state)),
      dispatch: vi.fn(),
      stateChanged: { connect: vi.fn() },
      commandResult: { connect: (listener: (json: string) => void) => { resultListener = listener; } },
      bridgeError: { connect: vi.fn() }
    };
    const bridge = await createQtBridge(proxy);
    const resultListenerFn = vi.fn();
    const attachmentListener = vi.fn();
    bridge.subscribeCommandResult(resultListenerFn);
    bridge.subscribeAttachmentEvents(attachmentListener);

    resultListener?.('{"type":"attachment.event","id":"web-9","payload":{"type":"attachment.init","attachmentId":"att-1","uploadId":"up-1","chunkSize":48128,"chunkIndex":0,"receivedIndexes":[],"expiresAt":"2026-09-03T00:00:00Z","content":""}}');

    expect(attachmentListener).toHaveBeenCalledWith(expect.objectContaining({
      type: 'attachment.event',
      id: 'web-9',
      payload: expect.objectContaining({ type: 'attachment.init' })
    }));
    expect(resultListenerFn).not.toHaveBeenCalled();
  });

  it('keeps routing plain command results away from the attachment subscription', async () => {
    let resultListener: ((json: string) => void) | undefined;
    const proxy = {
      currentStateJson: (callback: (json: string) => void) => callback(JSON.stringify(state)),
      dispatch: vi.fn(),
      stateChanged: { connect: vi.fn() },
      commandResult: { connect: (listener: (json: string) => void) => { resultListener = listener; } },
      bridgeError: { connect: vi.fn() }
    };
    const bridge = await createQtBridge(proxy);
    const attachmentListener = vi.fn();
    bridge.subscribeAttachmentEvents(attachmentListener);

    resultListener?.('{"id":"web-1","ok":true}');

    expect(attachmentListener).not.toHaveBeenCalled();
  });
```

并在文件顶部 import 处补类型（`createQtBridge, createWebChannelBridge` 行不变，types 行改为）：

```ts
import type { AttachmentEvent, BridgeState } from './types';
```

（若 lint 报 AttachmentEvent 未使用，可在第一个新用例里给 `attachmentListener` 标注类型：`const attachmentListener = vi.fn<(event: AttachmentEvent) => void>();`）

- [ ] **Step 2: 运行确认失败**

Run: `cd frontend && pnpm exec vitest run src/bridge/chatBridge.test.ts`
Expected: FAIL —— `bridge.subscribeAttachmentEvents is not a function`（TS 侧 `pnpm exec tsc --noEmit` 同时报 Property 'subscribeAttachmentEvents' does not exist）。

- [ ] **Step 3: types.ts 增加类型（规格 §7.2 + §9）**

在 `frontend/src/bridge/types.ts` 中：

(a) `MessageItem` 类型（第 30-39 行）增加可选附件字段与分组无关字段，改为：

```ts
export type MessageAttachment = {
  attachmentId: string;
  fileName: string;
  logicalSize: number;
  status?: 'available' | 'downloading' | 'verified-failed' | 'expired';
  receivedChunks?: number;
  totalChunks?: number;
};

export type MessageItem = {
  messageId: string;
  displayName: string;
  userCode: string;
  time: string;
  content: string;
  selfMessage: boolean;
  systemMessage: boolean;
  deliveryState?: 'queued' | 'sent' | 'delivered' | 'failed';
  attachment?: MessageAttachment;
};
```

(b) `RoomSummary` 增加 `mls?: boolean;`；`DirectMessageSummary` 增加 `mls?: boolean;`；`ConversationRef` 两个分支各增加 `mls?: boolean;`（ChatHeader 锁 chip 与侧栏锁徽标的数据源，C++ 未发布前缺省隐藏——规格 §7.5/§11）。

(c) 文件末尾（`ChatBridgeClient` 之前）追加附件事件类型（字段照抄契约）：

```ts
export type AttachmentEventPayload =
  | { type: 'attachment.init'; attachmentId: string; uploadId: string;
      chunkSize: number; chunkIndex: number; receivedIndexes: number[];
      expiresAt: string; content: string;
      logicalSize?: number }   // 规格 §11 #1：已请求 Codex 补充；缺省时 UI 走不定态降级
  | { type: 'attachment.chunk'; attachmentId: string; chunkIndex: number;
      content: string }
  | { type: 'attachment.resume'; attachmentId: string;
      receivedIndexes: number[] }
  | { type: 'error'; code: string; message: string };

export type AttachmentEvent = {
  type: 'attachment.event';
  id: string;
  payload: AttachmentEventPayload;
};
```

(d) `ChatBridgeClient` 接口增加一个订阅方法（放在 `subscribeCommandResult` 之后）：

```ts
  subscribeAttachmentEvents(listener: (event: AttachmentEvent) => void): () => void;
```

- [ ] **Step 4: chatBridge.ts 分流实现**

`frontend/src/bridge/chatBridge.ts`：

(a) 顶部 import 改为：

```ts
import type { AttachmentEvent, BridgeCommand, BridgeError, BridgeState, ChatBridgeClient, CommandResult } from './types';
```

(b) `FakeChatBridge`：新增字段（`errorListeners` 之后）：

```ts
  private readonly attachmentListeners = new Set<(event: AttachmentEvent) => void>();
```

新增方法（`subscribeCommandResult` 之后）：

```ts
  subscribeAttachmentEvents(listener: (event: AttachmentEvent) => void): () => void {
    this.attachmentListeners.add(listener);
    return () => this.attachmentListeners.delete(listener);
  }
```

新增测试辅助方法（`publish` 之后）：

```ts
  publishAttachmentEvent(event: AttachmentEvent): void {
    this.attachmentListeners.forEach((listener) => listener(event));
  }
```

(c) `QtChatBridge`：新增同名字段与方法（同 FakeChatBridge），并把构造器里的 commandResult 接线改为：

```ts
    proxy.commandResult.connect((json) => {
      const message = JSON.parse(json) as CommandResult | AttachmentEvent;
      if ('type' in message && message.type === 'attachment.event') {
        this.attachmentListeners.forEach((listener) => listener(message));
        return;
      }
      this.resultListeners.forEach((listener) => listener(message));
    });
```

- [ ] **Step 5: MessageComposer.test.tsx 的 ControllableBridge 补齐接口**

`frontend/src/components/MessageComposer.test.tsx` 的 `ControllableBridge` 类（第 21-34 行）改为：

```ts
class ControllableBridge implements ChatBridgeClient {
  readonly commands: BridgeCommand[] = [];
  private readonly resultListeners = new Set<(result: CommandResult) => void>();
  private readonly attachmentListeners = new Set<(event: AttachmentEvent) => void>();

  currentStateJson() { return JSON.stringify(connectedRoomState); }
  dispatch(command: BridgeCommand) { this.commands.push(command); }
  subscribe() { return () => undefined; }
  subscribeCommandResult(listener: (result: CommandResult) => void) {
    this.resultListeners.add(listener);
    return () => this.resultListeners.delete(listener);
  }
  subscribeAttachmentEvents(listener: (event: AttachmentEvent) => void) {
    this.attachmentListeners.add(listener);
    return () => this.attachmentListeners.delete(listener);
  }
  subscribeBridgeError() { return () => undefined; }
  publishCommandResult(result: CommandResult) { this.resultListeners.forEach((listener) => listener(result)); }
  publishAttachmentEvent(event: AttachmentEvent) { this.attachmentListeners.forEach((listener) => listener(event)); }
}
```

顶部 import 改为：

```ts
import type { AttachmentEvent, BridgeCommand, BridgeError, BridgeState, ChatBridgeClient, CommandResult } from '../bridge/types';
```

- [ ] **Step 6: 运行测试确认通过**

Run: `cd frontend && pnpm exec vitest run src/bridge/chatBridge.test.ts src/components/MessageComposer.test.tsx && pnpm exec tsc --noEmit`
Expected: 两个测试文件全绿；tsc 无错误。

- [ ] **Step 7: 提交**

```bash
git add frontend/src/bridge/types.ts frontend/src/bridge/chatBridge.ts frontend/src/bridge/chatBridge.test.ts frontend/src/components/MessageComposer.test.tsx
git commit -m "feat(bridge): route attachment.event frames from the command-result channel" -- frontend/src/bridge/types.ts frontend/src/bridge/chatBridge.ts frontend/src/bridge/chatBridge.test.ts frontend/src/components/MessageComposer.test.tsx
```

---

### Task 2: 附件上传事件归一 reducer（纯函数）与错误文案映射

**Files:**
- Create: `frontend/src/bridge/attachmentEvents.ts`
- Test: `frontend/src/bridge/attachmentEvents.test.ts`

- [ ] **Step 1: 写失败测试**

创建 `frontend/src/bridge/attachmentEvents.test.ts`：

```ts
import { describe, expect, it } from 'vitest';

import type { AttachmentEvent } from './types';
import {
  attachmentErrorCopy,
  beginAttachmentUpload,
  dismissAttachmentUpload,
  reduceAttachmentEvent
} from './attachmentEvents';

function initEvent(id: string, receivedIndexes: number[] = [], logicalSize?: number): AttachmentEvent {
  return {
    type: 'attachment.event',
    id,
    payload: {
      type: 'attachment.init', attachmentId: 'att-1234567890abcdef', uploadId: 'up-1',
      chunkSize: 48128, chunkIndex: 0, receivedIndexes,
      expiresAt: '2026-09-03T00:00:00Z', content: '',
      ...(logicalSize !== undefined ? { logicalSize } : {})
    }
  };
}

function chunkEvent(id: string, chunkIndex: number): AttachmentEvent {
  return {
    type: 'attachment.event', id,
    payload: { type: 'attachment.chunk', attachmentId: 'att-1234567890abcdef', chunkIndex, content: '' }
  };
}

describe('beginAttachmentUpload', () => {
  it('registers a choosing card keyed by the chooseUpload command id', () => {
    const next = beginAttachmentUpload({}, 'web-1');
    expect(next['web-1']).toMatchObject({ phase: 'choosing', receivedChunks: 0, lastChunkIndex: -1 });
  });
});

describe('reduceAttachmentEvent', () => {
  it('turns an init with an empty baseline into an uploading card', () => {
    const next = reduceAttachmentEvent({}, initEvent('web-1'));
    expect(next['web-1']).toMatchObject({
      phase: 'uploading', attachmentId: 'att-1234567890abcdef', uploadId: 'up-1',
      chunkSize: 48128, receivedChunks: 0, lastChunkIndex: 0, expiresAt: '2026-09-03T00:00:00Z'
    });
    expect(next['web-1'].totalChunks).toBeUndefined();
  });

  it('derives totalChunks from logicalSize when Codex supplies it', () => {
    const next = reduceAttachmentEvent({}, initEvent('web-1', [], 48128 * 3));
    expect(next['web-1'].totalChunks).toBe(3);
  });

  it('turns an init with a non-empty baseline into a resuming card', () => {
    const next = reduceAttachmentEvent({}, initEvent('web-1', [0, 1, 2]));
    expect(next['web-1']).toMatchObject({ phase: 'resuming', receivedChunks: 3 });
  });

  it('counts chunk acks per command id', () => {
    let map = reduceAttachmentEvent({}, initEvent('web-1'));
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 1));
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 2));
    expect(map['web-1']).toMatchObject({ phase: 'uploading', receivedChunks: 2, lastChunkIndex: 2 });
  });

  it('ignores chunk acks for unknown command ids', () => {
    const next = reduceAttachmentEvent({}, chunkEvent('web-unknown', 1));
    expect(next).toEqual({});
  });

  it('flips to completed when every derived chunk is acknowledged', () => {
    let map = reduceAttachmentEvent({}, initEvent('web-1', [], 48128 * 2));
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 0));
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 1));
    expect(map['web-1'].phase).toBe('completed');
  });

  it('marks a resume event as resuming with the reported baseline', () => {
    let map = reduceAttachmentEvent({}, initEvent('web-1'));
    map = reduceAttachmentEvent(map, {
      type: 'attachment.event', id: 'web-1',
      payload: { type: 'attachment.resume', attachmentId: 'att-1234567890abcdef', receivedIndexes: [0, 1] }
    });
    expect(map['web-1']).toMatchObject({ phase: 'resuming', receivedChunks: 2 });
  });

  it('marks an error payload as failed and keeps the raw code', () => {
    let map = reduceAttachmentEvent({}, initEvent('web-1'));
    map = reduceAttachmentEvent(map, {
      type: 'attachment.event', id: 'web-1',
      payload: { type: 'error', code: 'ErrRoomQuotaExceeded', message: 'quota' }
    });
    expect(map['web-1']).toMatchObject({
      phase: 'failed',
      error: { code: 'ErrRoomQuotaExceeded', message: 'quota' }
    });
  });

  it('does not resurrect a failed card when late chunk acks arrive', () => {
    let map = reduceAttachmentEvent({}, initEvent('web-1'));
    map = reduceAttachmentEvent(map, {
      type: 'attachment.event', id: 'web-1',
      payload: { type: 'error', code: 'ErrAttachmentChunkHashMismatch', message: 'hash' }
    });
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 5));
    expect(map['web-1'].phase).toBe('failed');
  });
});

describe('dismissAttachmentUpload', () => {
  it('removes the card without touching other concurrent uploads', () => {
    let map = beginAttachmentUpload({}, 'web-1');
    map = beginAttachmentUpload(map, 'web-2');
    map = dismissAttachmentUpload(map, 'web-1');
    expect(Object.keys(map)).toEqual(['web-2']);
  });
});

describe('attachmentErrorCopy', () => {
  it('maps the ten server sentinels to user copy', () => {
    expect(attachmentErrorCopy('ErrAttachmentTooLarge', '')).toBe('文件超过 500 MiB 上限');
    expect(attachmentErrorCopy('ErrRoomQuotaExceeded', '')).toBe('房间附件配额已满（20 GiB）');
    expect(attachmentErrorCopy('ErrInvalidAttachmentSize', '')).toBe('文件大小无效');
    expect(attachmentErrorCopy('ErrAttachmentUploadNotFound', '')).toBe('传输异常，请重试');
    expect(attachmentErrorCopy('ErrAttachmentUploadUnauthorized', '')).toBe('没有在此频道发送附件的权限');
    expect(attachmentErrorCopy('ErrAttachmentUploadExpired', '')).toBe('上传会话已过期（24 小时），请重新发送');
    expect(attachmentErrorCopy('ErrAttachmentChunkOutOfRange', '')).toBe('传输异常，请重试');
    expect(attachmentErrorCopy('ErrAttachmentChunkTooLarge', '')).toBe('传输异常，请重试');
    expect(attachmentErrorCopy('ErrAttachmentChunkHashMismatch', '')).toBe('分块校验失败，正在自动重传…');
    expect(attachmentErrorCopy('ErrAttachmentChunkConflict', '')).toBe('传输异常，请重试');
  });

  it('falls back to the bridge message for unaligned codes', () => {
    expect(attachmentErrorCopy('ErrSomethingNew', 'bridge said no')).toBe('bridge said no');
    expect(attachmentErrorCopy('ErrSomethingNew', '')).toBe('附件上传失败');
  });
});
```

- [ ] **Step 2: 运行确认失败**

Run: `cd frontend && pnpm exec vitest run src/bridge/attachmentEvents.test.ts`
Expected: FAIL —— `Cannot find module './attachmentEvents'`。

- [ ] **Step 3: 实现 reducer**

创建 `frontend/src/bridge/attachmentEvents.ts`：

```ts
import type { AttachmentEvent } from './types';

export type AttachmentUploadPhase = 'choosing' | 'uploading' | 'resuming' | 'completed' | 'failed';

export type AttachmentUploadState = {
  phase: AttachmentUploadPhase;
  attachmentId?: string;
  uploadId?: string;
  chunkSize?: number;
  totalChunks?: number;
  receivedChunks: number;
  lastChunkIndex: number;
  expiresAt?: string;
  error?: { code: string; message: string };
};

export type AttachmentUploadMap = Record<string, AttachmentUploadState>;

export function beginAttachmentUpload(map: AttachmentUploadMap, commandId: string): AttachmentUploadMap {
  return { ...map, [commandId]: { phase: 'choosing', receivedChunks: 0, lastChunkIndex: -1 } };
}

export function dismissAttachmentUpload(map: AttachmentUploadMap, commandId: string): AttachmentUploadMap {
  const next = { ...map };
  delete next[commandId];
  return next;
}

function withTotalChunks(
  state: AttachmentUploadState,
  chunkSize: number,
  logicalSize: number | undefined
): AttachmentUploadState {
  const totalChunks = logicalSize && logicalSize > 0 ? Math.ceil(logicalSize / chunkSize) : undefined;
  if (totalChunks === undefined) return state;
  return { ...state, totalChunks, phase: state.receivedChunks >= totalChunks ? 'completed' : state.phase };
}

export function reduceAttachmentEvent(map: AttachmentUploadMap, event: AttachmentEvent): AttachmentUploadMap {
  const current = map[event.id];
  const payload = event.payload;

  if (payload.type === 'attachment.init') {
    const baseline: AttachmentUploadState = {
      phase: payload.receivedIndexes.length > 0 ? 'resuming' : 'uploading',
      attachmentId: payload.attachmentId,
      uploadId: payload.uploadId,
      chunkSize: payload.chunkSize,
      receivedChunks: payload.receivedIndexes.length,
      lastChunkIndex: payload.chunkIndex,
      expiresAt: payload.expiresAt
    };
    return { ...map, [event.id]: withTotalChunks(baseline, payload.chunkSize, payload.logicalSize) };
  }

  if (!current || current.phase === 'failed') return map;

  if (payload.type === 'attachment.chunk') {
    const advanced: AttachmentUploadState = {
      ...current,
      phase: 'uploading',
      receivedChunks: current.receivedChunks + 1,
      lastChunkIndex: payload.chunkIndex
    };
    return {
      ...map,
      [event.id]: current.totalChunks !== undefined && advanced.receivedChunks >= current.totalChunks
        ? { ...advanced, phase: 'completed' }
        : advanced
    };
  }

  if (payload.type === 'attachment.resume') {
    const resumed: AttachmentUploadState = {
      ...current,
      phase: 'resuming',
      receivedChunks: payload.receivedIndexes.length
    };
    return {
      ...map,
      [event.id]: current.totalChunks !== undefined && resumed.receivedChunks >= current.totalChunks
        ? { ...resumed, phase: 'completed' }
        : resumed
    };
  }

  return {
    ...map,
    [event.id]: { ...current, phase: 'failed', error: { code: payload.code, message: payload.message } }
  };
}

const attachmentErrorCopyTable: Record<string, string> = {
  ErrAttachmentTooLarge: '文件超过 500 MiB 上限',
  ErrRoomQuotaExceeded: '房间附件配额已满（20 GiB）',
  ErrInvalidAttachmentSize: '文件大小无效',
  ErrAttachmentUploadNotFound: '传输异常，请重试',
  ErrAttachmentUploadUnauthorized: '没有在此频道发送附件的权限',
  ErrAttachmentUploadExpired: '上传会话已过期（24 小时），请重新发送',
  ErrAttachmentChunkOutOfRange: '传输异常，请重试',
  ErrAttachmentChunkTooLarge: '传输异常，请重试',
  ErrAttachmentChunkHashMismatch: '分块校验失败，正在自动重传…',
  ErrAttachmentChunkConflict: '传输异常，请重试'
};

export function attachmentErrorCopy(code: string, fallback: string): string {
  return attachmentErrorCopyTable[code] ?? (fallback || '附件上传失败');
}
```

- [ ] **Step 4: 运行确认通过**

Run: `cd frontend && pnpm exec vitest run src/bridge/attachmentEvents.test.ts && pnpm exec tsc --noEmit`
Expected: 全绿（13 用例）；tsc 无错误。

- [ ] **Step 5: 提交**

```bash
git add frontend/src/bridge/attachmentEvents.ts frontend/src/bridge/attachmentEvents.test.ts
git commit -m "feat(bridge): normalize attachment upload event streams per command id" -- frontend/src/bridge/attachmentEvents.ts frontend/src/bridge/attachmentEvents.test.ts
```

---

### Task 3: 动效双门（`data-effects` + motionAllowed）

**Files:**
- Modify: `frontend/src/animation/motion.ts`
- Modify: `frontend/src/app/App.tsx`（写入 `<html data-effects>`）
- Test: `frontend/src/animation/motion.test.ts`

- [ ] **Step 1: 写失败测试（motion.test.ts 追加用例）**

在 `frontend/src/animation/motion.test.ts` 的 import 行改为：

```ts
import { animateWorkspacePanels, motionAllowed } from './motion';
```

`describe` 块内追加：

```ts
  it('reports motion as allowed while the effects gate is unset', () => {
    delete document.documentElement.dataset.effects;
    expect(motionAllowed()).toBe(true);
  });

  it('skips tweens and reports blocked motion when data-effects is off', () => {
    document.documentElement.dataset.effects = 'off';
    expect(motionAllowed()).toBe(false);

    const root = document.createElement('main');
    animateWorkspacePanels(root);
    animateMessage(root);
    expect(fromTo).not.toHaveBeenCalled();
    delete document.documentElement.dataset.effects;
  });
```

并在顶部 import 追加 `animateMessage`：

```ts
import { animateMessage, animateWorkspacePanels, motionAllowed } from './motion';
```

- [ ] **Step 2: 运行确认失败**

Run: `cd frontend && pnpm exec vitest run src/animation/motion.test.ts`
Expected: FAIL —— `motionAllowed is not exported`。

- [ ] **Step 3: 实现 motion.ts 双门**

`frontend/src/animation/motion.ts` 全文改为：

```ts
import { gsap } from 'gsap';

function prefersReducedMotion() {
  return typeof window !== 'undefined' && typeof window.matchMedia === 'function'
    && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
}

export function motionAllowed(): boolean {
  if (prefersReducedMotion()) return false;
  return typeof document === 'undefined' || document.documentElement.dataset.effects !== 'off';
}

export function animateWorkspacePanels(root: Element) {
  if (!motionAllowed()) return () => undefined;
  const context = gsap.context(() => {
    gsap.fromTo(root,
      { opacity: 0, transform: 'translateY(4px)' },
      {
        opacity: 1,
        transform: 'translateY(0px)',
        duration: 0.16,
        ease: 'power3.out',
        force3D: true,
        overwrite: 'auto',
        clearProps: 'transform,opacity'
      }
    );
  }, root);
  return () => { context.revert(); };
}

export function animateMessage(element: HTMLElement | null) {
  if (!element || !motionAllowed()) return () => undefined;
  const tween = gsap.fromTo(element,
    { opacity: 0, transform: 'translateY(6px)' },
    { opacity: 1, transform: 'translateY(0px)', duration: 0.18, ease: 'power3.out', clearProps: 'transform,opacity' }
  );
  return () => { tween.kill(); };
}

export function animatePopover(element: HTMLElement | null) {
  if (!element || !motionAllowed()) return () => undefined;
  const tween = gsap.fromTo(element,
    { opacity: 0, transform: 'translateY(4px) scale(0.96)', transformOrigin: 'bottom right' },
    { opacity: 1, transform: 'translateY(0px) scale(1)', duration: 0.18, ease: 'power3.out', clearProps: 'transform,opacity,transformOrigin' }
  );
  return () => { tween.kill(); };
}
```

（三个动画函数的门从 `prefersReducedMotion()` 换成 `motionAllowed()`——规格 §6.2 的「第二个门」。）

- [ ] **Step 4: App.tsx 写入 `<html data-effects>`（对齐现有 data-theme 模式）**

`frontend/src/app/App.tsx` 中，把：

```tsx
  useEffect(() => {
    document.documentElement.dataset.theme = settings.darkTheme ? 'dark' : 'light';
  }, [settings.darkTheme]);
```

替换为：

```tsx
  useEffect(() => {
    document.documentElement.dataset.theme = settings.darkTheme ? 'dark' : 'light';
  }, [settings.darkTheme]);

  const effectsEnabled = state.performance?.effectsEnabled !== false;

  useEffect(() => {
    document.documentElement.dataset.effects = effectsEnabled ? 'on' : 'off';
  }, [effectsEnabled]);
```

（`effectsEnabled` 当前由 C++ performance 状态驱动；bridge 未发布该字段时缺省开启——这是对 types.ts:81 死字段的接线，规格 §6.2。）

- [ ] **Step 5: 运行确认通过**

Run: `cd frontend && pnpm exec vitest run src/animation/motion.test.ts src/app/App.test.tsx && pnpm exec tsc --noEmit`
Expected: 全绿；tsc 无错误。

- [ ] **Step 6: 提交**

```bash
git add frontend/src/animation/motion.ts frontend/src/animation/motion.test.ts frontend/src/app/App.tsx
git commit -m "feat(motion): gate gsap and css effects on performance.effectsEnabled" -- frontend/src/animation/motion.ts frontend/src/animation/motion.test.ts frontend/src/app/App.tsx
```

---

### Task 4: CSS 令牌层 tokens.css + global.css 改 import hub

**Files:**
- Create: `frontend/src/styles/tokens.css`
- Modify: `frontend/src/styles/global.css`（整文件替换为 import hub）

> 说明：这是对规格 §9「重写 global.css」的唯一结构偏离——把单一 `--t-*` 层拆成 tokens/pages/workspace 三个 partial，便于按任务小步提交与回滚；令牌来源仍是唯一的 tokens.css。

- [ ] **Step 1: 写 tokens.css（完整内容如下，无省略）**

创建 `frontend/src/styles/tokens.css`：

```css
/* T3 令牌层 — 唯一令牌来源（规格 §3）。旧雾蓝三层样式全部废弃。 */
:root {
  --t-accent: #2F7BE5;
  --t-accent-soft: #E7F0FC;
  --t-bubble-out: #E3FEE0;
  --t-bubble-in: #FFFFFF;
  --t-wall: #EDEEF0;
  --t-ink: #1D1F22;
  --t-ink-2: #70767E;
  --t-ink-3: #9AA1A9;
  --t-online: #2EA760;
  --t-warn: #B87926;
  --t-danger: #C93B3B;
  --t-line: #E4E6EA;
  --t-chip: #F1F2F4;
  --t-bubble-radius: 12px;
  --t-bubble-shadow: 0 1px 1px rgba(0, 0, 0, 0.10);
  --t-surface: #FFFFFF;
  --t-drawer-shadow: -8px 0 24px rgba(15, 23, 32, 0.14);
  --t-font: -apple-system, "Segoe UI", "Microsoft YaHei", "PingFang SC", sans-serif;
  --t-fast: 180ms;
}

* { box-sizing: border-box; margin: 0; padding: 0; }
html, body, #root { height: 100%; }
body {
  font-family: var(--t-font);
  color: var(--t-ink);
  background: var(--t-wall);
  font-size: 14px;
  line-height: 1.5;
}
h1 { font-size: 22px; }
h2 { font-size: 16px; }
h3 { font-size: 14px; }
button, input, select, textarea { font: inherit; color: inherit; }
a { color: var(--t-accent); }

.eyebrow {
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  color: var(--t-ink-3);
}
.lede { color: var(--t-ink-2); font-size: 14px; }

/* 按钮 */
.primary-button, .secondary-button, .danger-button {
  border: 1px solid transparent;
  border-radius: 8px;
  padding: 8px 16px;
  font-size: 13.5px;
  font-weight: 600;
  cursor: pointer;
  transition: background-color var(--t-fast) ease-out, border-color var(--t-fast) ease-out;
}
.primary-button { background: var(--t-accent); color: #fff; }
.primary-button:hover:not(:disabled) { background: #2A6DC9; }
.secondary-button { background: var(--t-surface); border-color: var(--t-line); color: var(--t-ink); }
.secondary-button:hover:not(:disabled) { background: var(--t-chip); }
.danger-button { background: var(--t-danger); color: #fff; }
.danger-button:hover:not(:disabled) { background: #B33333; }
.primary-button:disabled, .secondary-button:disabled, .danger-button:disabled { opacity: 0.5; cursor: not-allowed; }

.icon-button {
  border: 0; background: transparent; color: var(--t-ink-2);
  width: 30px; height: 30px; border-radius: 8px; font-size: 15px;
  cursor: pointer; display: inline-flex; align-items: center; justify-content: center;
  transition: background-color var(--t-fast) ease-out, color var(--t-fast) ease-out;
}
.icon-button:hover { background: var(--t-chip); color: var(--t-ink); }

/* 表单 */
input[type='text'], input[type='search'], input[type='password'], input:not([type]), textarea, select {
  border: 1px solid var(--t-line);
  border-radius: 8px;
  background: var(--t-surface);
  padding: 8px 10px;
  color: var(--t-ink);
  outline: none;
  transition: border-color var(--t-fast) ease-out, box-shadow var(--t-fast) ease-out;
}
input:focus-visible, textarea:focus-visible, select:focus-visible {
  border-color: var(--t-accent);
  box-shadow: 0 0 0 3px var(--t-accent-soft);
}
input[type='checkbox'] { accent-color: var(--t-accent); }

.form-grid { display: grid; grid-template-columns: 120px minmax(0, 1fr); gap: 10px 12px; align-items: center; }
.form-grid label { font-size: 13px; color: var(--t-ink-2); }
.input-with-action { display: flex; flex-direction: column; gap: 4px; }
.input-action-row { display: flex; gap: 8px; }
.input-action-row input { flex: 1; min-width: 0; }
.field-hint { color: var(--t-ink-3); font-size: 12px; }
.form-actions { display: flex; justify-content: flex-end; gap: 10px; margin-top: 18px; }

/* 头像与在场 */
.avatar {
  width: 40px; height: 40px; border-radius: 50%; flex: none;
  display: inline-flex; align-items: center; justify-content: center;
  background: var(--t-accent); color: #fff; font-size: 15px; font-weight: 600;
  user-select: none;
}
.avatar--small { width: 34px; height: 34px; font-size: 13px; }
.avatar--tiny { width: 22px; height: 22px; font-size: 10px; }
.avatar--offset { margin-left: -8px; }
.avatar-stack { display: inline-flex; align-items: center; }
.presence-dot { width: 8px; height: 8px; border-radius: 50%; background: var(--t-ink-3); flex: none; }
.presence-dot--online { background: var(--t-online); }
.conversation-copy { min-width: 0; flex: 1; display: flex; flex-direction: column; }
.conversation-copy strong { font-size: 13.5px; font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.conversation-copy small { color: var(--t-ink-2); font-size: 12px; }

/* 模态 */
.modal-backdrop {
  position: fixed; inset: 0; background: rgba(29, 31, 34, 0.32);
  display: flex; align-items: center; justify-content: center; z-index: 40;
  animation: t-fade-in var(--t-fast) ease-out both;
}
.modal-surface {
  background: var(--t-surface); border-radius: 12px;
  box-shadow: 0 16px 48px rgba(15, 23, 32, 0.18);
  width: min(420px, calc(100vw - 48px));
  max-height: min(560px, calc(100vh - 96px));
  overflow-y: auto;
  padding: 14px 16px 16px;
  animation: t-pop-in var(--t-fast) ease-out both;
}
.sidebar-heading { display: flex; align-items: center; justify-content: space-between; gap: 8px; margin-bottom: 8px; }
.sidebar-heading h2 { font-size: 15px; }
.sidebar-actions { display: flex; gap: 4px; }

/* 命令反馈（规格 §5.6：pending 追加 12px 旋转环） */
.command-feedback {
  display: flex; align-items: center; gap: 7px;
  font-size: 12.5px; color: var(--t-ink-2); margin: 6px 0 0;
}
.command-feedback--error { color: var(--t-danger); }
.command-feedback--success { color: var(--t-online); }
.command-feedback__spinner {
  width: 12px; height: 12px; border-radius: 50%; flex: none;
  border: 1.6px solid var(--t-accent-soft); border-top-color: var(--t-accent);
  animation: t-spin 0.8s linear infinite;
}

.status-line { display: inline-flex; align-items: center; gap: 7px; font-size: 12.5px; color: var(--t-ink-2); }
.status-line--error { color: var(--t-danger); }

/* 进度条：只用 transform: scaleX（规格 §6.3） */
.progress-track { height: 4px; border-radius: 999px; background: rgba(0, 0, 0, 0.08); overflow: hidden; margin-top: 5px; }
.progress-fill {
  display: block; height: 100%; background: var(--t-accent);
  transform-origin: left; transform: scaleX(0);
  transition: transform var(--t-fast) ease-out;
}
.progress-fill--indeterminate { animation: t-indeterminate 1.1s ease-in-out infinite alternate; }
.progress-fill--warn { background: var(--t-warn); }
.progress-fill--done { background: var(--t-online); }

/* keyframes：0.8s linear infinite 的 spinner 是过渡预算的显式例外（规格 §6.2） */
@keyframes t-spin { to { transform: rotate(360deg); } }
@keyframes t-pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.35; } }
@keyframes t-fade-in { from { opacity: 0; } to { opacity: 1; } }
@keyframes t-pop-in { from { opacity: 0; transform: translateY(6px) scale(0.98); } to { opacity: 1; transform: translateY(0) scale(1); } }
@keyframes t-indeterminate { from { transform: scaleX(0.08); } to { transform: scaleX(1); } }

/* 双重动效门：off 或系统减少动效时，循环动画冻结在静止帧（环/条的边框仍在），过渡直达终态 */
html[data-effects='off'] *, html[data-effects='off'] *::before, html[data-effects='off'] *::after {
  animation: none !important;
  transition: none !important;
}
@media (prefers-reduced-motion: reduce) {
  *, *::before, *::after { animation: none !important; transition: none !important; }
}
```

- [ ] **Step 2: global.css 替换为 import hub（完整内容）**

用 Write 工具把 `frontend/src/styles/global.css` 全文替换为：

```css
/* T3 入口：三个 partial，全部消费 tokens.css 的 --t-* 令牌。 */
@import './tokens.css';
@import './pages.css';
@import './workspace.css';
```

- [ ] **Step 3: 创建 pages.css 与 workspace.css 的占位**

本任务先建两个空占位（内容在 Task 5/6 填充），保证 import 不 404：

创建 `frontend/src/styles/pages.css`：

```css
/* pages 样式在 Task 5 填充。 */
```

创建 `frontend/src/styles/workspace.css`：

```css
/* workspace 样式在 Task 6 填充。 */
```

- [ ] **Step 4: 验证**

Run: `cd frontend && pnpm exec vitest run && pnpm exec tsc --noEmit`
Expected: 全绿（此时页面样式暂时退化为默认样式，属预期——Task 5/6 恢复）。再运行 `pnpm build` 确认 CSS import 可构建。

Run: `cd frontend && pnpm build`
Expected: `vite build` 输出 `built in ...`，无 CSS 报错。

- [ ] **Step 5: 提交**

```bash
git add frontend/src/styles/tokens.css frontend/src/styles/global.css frontend/src/styles/pages.css frontend/src/styles/workspace.css
git commit -m "feat(styles): replace three-layer legacy css with the t3 token layer" -- frontend/src/styles/tokens.css frontend/src/styles/global.css frontend/src/styles/pages.css frontend/src/styles/workspace.css
```

---

### Task 5: pages.css — 模式/连接/宿主/设置页与弹窗内容

**Files:**
- Modify: `frontend/src/styles/pages.css`（整文件替换）

- [ ] **Step 1: 写入完整内容**

用 Write 工具把 `frontend/src/styles/pages.css` 全文替换为：

```css
/* 页面骨架 + 模式/连接/宿主/设置页 + 弹窗内容（T3）。 */
.app-shell { min-height: 100vh; background: var(--t-wall); }

.mode-shell, .connect-shell, .settings-shell {
  display: flex; justify-content: center; align-items: flex-start;
  padding: 48px 20px 64px; height: 100vh; overflow-y: auto;
}

.mode-panel, .connect-panel, .settings-panel {
  background: var(--t-surface);
  border: 1px solid var(--t-line);
  border-radius: 12px;
  box-shadow: 0 10px 30px rgba(15, 23, 32, 0.08);
  padding: 28px;
}
.mode-panel { width: min(760px, 100%); }
.connect-panel { width: min(460px, 100%); }
.settings-panel { width: min(640px, 100%); }
.host-panel { width: min(520px, 100%); }

.mode-panel h1, .connect-panel h1 { margin: 2px 0 8px; }
.mode-panel .lede { margin-bottom: 20px; }

/* 模式选择 */
.mode-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(220px, 1fr)); gap: 12px; margin-top: 18px; }
.mode-card {
  display: flex; flex-direction: column; gap: 4px; text-align: left;
  background: var(--t-surface); border: 1px solid var(--t-line); border-radius: 12px;
  padding: 16px; cursor: pointer;
  transition: border-color var(--t-fast) ease-out, box-shadow var(--t-fast) ease-out, transform var(--t-fast) ease-out;
}
.mode-card:hover:not(:disabled) { border-color: var(--t-accent); box-shadow: var(--t-bubble-shadow); transform: translateY(-1px); }
.mode-card:disabled { opacity: 0.55; cursor: not-allowed; }
.mode-card strong { font-size: 14.5px; }
.mode-card span:last-child { color: var(--t-ink-2); font-size: 12.5px; }
.mode-icon {
  width: 34px; height: 34px; border-radius: 10px; background: var(--t-accent-soft); color: var(--t-accent);
  display: inline-flex; align-items: center; justify-content: center; font-size: 16px; margin-bottom: 6px;
}

/* 连接页 */
.connect-panel h1 { font-size: 19px; }
.connection-help { color: var(--t-ink-2); font-size: 12.5px; margin: 6px 0 14px; line-height: 1.6; }
.connect-panel .status-line { margin-bottom: 8px; }

.tunnel-guide {
  background: var(--t-accent-soft); border-radius: 10px; padding: 10px 12px; margin-bottom: 14px;
  font-size: 12.5px; color: var(--t-ink-2);
}
.tunnel-guide strong { color: var(--t-accent); display: block; margin-bottom: 2px; }

/* LAN 发现（含规格 §6.1 #5 的顶部扫描细条） */
.lan-discovery { border: 1px solid var(--t-line); border-radius: 10px; padding: 12px; margin-bottom: 14px; position: relative; overflow: hidden; }
.lan-discovery__header { display: flex; align-items: center; justify-content: space-between; gap: 8px; margin-bottom: 8px; }
.lan-discovery__header strong { font-size: 13.5px; display: block; }
.lan-discovery__header small { color: var(--t-ink-2); font-size: 12px; }
.lan-discovery__empty { color: var(--t-ink-2); font-size: 12.5px; line-height: 1.6; }
.lan-discovery__list { display: flex; flex-direction: column; gap: 6px; }
.lan-host-card {
  display: flex; align-items: center; justify-content: space-between; gap: 8px; text-align: left;
  border: 1px solid var(--t-line); border-radius: 10px; background: var(--t-surface);
  padding: 9px 11px; cursor: pointer;
  transition: border-color var(--t-fast) ease-out, background-color var(--t-fast) ease-out;
}
.lan-host-card:hover { background: var(--t-chip); }
.lan-host-card--selected, .lan-host-card--selected:hover { border-color: var(--t-accent); background: var(--t-accent-soft); }
.lan-host-card strong { font-size: 13px; display: block; }
.lan-host-card small { color: var(--t-ink-2); font-size: 11.5px; }
.lan-host-card__trust { font-size: 11.5px; color: var(--t-accent); flex: none; }
.lan-discovery__notice { margin-top: 10px; font-size: 12px; color: var(--t-ink-2); }
.lan-discovery__confirmation { display: flex; align-items: center; gap: 6px; margin-top: 6px; font-size: 12px; }
.manual-entry-button {
  background: transparent; border: 0; color: var(--t-accent);
  font-size: 12.5px; cursor: pointer; padding: 0; margin-bottom: 12px;
}
.manual-entry-button:hover { text-decoration: underline; }

.lan-scan-bar { position: absolute; top: 0; left: 0; right: 0; height: 2px; background: var(--t-accent-soft); }
.lan-scan-bar__fill {
  display: block; height: 100%; background: var(--t-accent);
  transform-origin: left;
  animation: t-indeterminate 1.1s ease-in-out infinite alternate;
}

/* 设置页 */
.settings-header { display: flex; align-items: center; gap: 12px; margin-bottom: 16px; }
.settings-back { width: 32px; height: 32px; padding: 0; font-size: 18px; }
.settings-group { border-top: 1px solid var(--t-line); padding: 12px 0 4px; margin-top: 10px; }
.settings-group h2 { font-size: 13px; color: var(--t-ink-2); margin-bottom: 8px; }
.settings-row { display: flex; align-items: center; justify-content: space-between; gap: 12px; padding: 7px 0; font-size: 13px; }
.settings-row span:first-child { color: var(--t-ink-2); }
.settings-value { color: var(--t-ink); font-size: 12.5px; text-align: right; overflow-wrap: anywhere; }
.settings-toggle { cursor: pointer; }
.settings-toggle input { width: 16px; height: 16px; }
.settings-select-row select { min-width: 140px; }
.settings-note { color: var(--t-ink-3); font-size: 12px; margin: 6px 0; line-height: 1.6; }

/* 弹窗内容：成员资料 / 管理 / 审批 / 新建频道 */
.admin-profile { margin-bottom: 10px; }
.admin-profile p { color: var(--t-ink-2); font-size: 12.5px; margin-top: 2px; }
.admin-member-input { width: 100%; margin-bottom: 8px; }
.admin-actions { display: flex; gap: 8px; flex-wrap: wrap; margin-top: 10px; }
.connection-approval-list { display: flex; flex-direction: column; gap: 12px; }
.connection-approval-item strong { font-size: 13.5px; }
.connection-approval-item p { color: var(--t-ink-2); font-size: 12.5px; margin: 4px 0 2px; line-height: 1.6; }

.room-form__field { display: flex; flex-direction: column; gap: 6px; font-size: 13px; color: var(--t-ink-2); margin-bottom: 10px; }
.room-form__check { display: flex; align-items: center; gap: 6px; font-size: 13px; color: var(--t-ink-2); margin-bottom: 10px; }
.room-form__check input { width: 15px; height: 15px; }
.room-form__hint { color: var(--t-warn); font-size: 12px; margin: -4px 0 8px; }
.room-form__actions { display: flex; justify-content: flex-end; gap: 10px; margin-top: 12px; }

/* 响应式：模式卡片在窄屏单列 */
@media (max-width: 560px) {
  .mode-grid { grid-template-columns: 1fr; }
  .form-grid { grid-template-columns: 1fr; }
}
```

- [ ] **Step 2: 验证**

Run: `cd frontend && pnpm exec vitest run && pnpm exec tsc --noEmit && pnpm build`
Expected: 全绿 + 构建通过。

- [ ] **Step 3: 提交**

```bash
git add frontend/src/styles/pages.css
git commit -m "feat(styles): t3 pages layer for mode, connect, host and settings" -- frontend/src/styles/pages.css
```

---

### Task 6: workspace.css — 双栏、侧栏、气泡、抽屉、附件卡

**Files:**
- Modify: `frontend/src/styles/workspace.css`（整文件替换）

- [ ] **Step 1: 写入完整内容**

用 Write 工具把 `frontend/src/styles/workspace.css` 全文替换为：

```css
/* 工作区（T3 双栏 + 覆盖抽屉）。选择器沿用现有 JSX 类名，新增类见各节注释。 */
.workspace-shell {
  display: grid;
  grid-template-columns: 292px minmax(0, 1fr);
  height: 100vh;
  background: var(--t-surface);
  position: relative;
  overflow: hidden;
}

/* ---- 侧栏（已合并原 WorkspaceRail 职能：brand + 群/私 分段 + ⚙ + ☰） ---- */
.conversation-sidebar {
  background: var(--t-surface);
  border-right: 1px solid var(--t-line);
  display: flex; flex-direction: column; min-width: 0;
  padding: 0 0 8px;
}
.sidebar-top {
  display: flex; align-items: center; gap: 8px;
  padding: 10px 12px; border-bottom: 1px solid var(--t-line);
}
.brand-mark { width: 30px; height: 30px; border-radius: 8px; object-fit: contain; flex: none; }
.section-seg { display: flex; background: var(--t-chip); border-radius: 8px; padding: 2px; gap: 2px; flex: 1; }
.section-seg button {
  flex: 1; border: 0; background: transparent; font-size: 13px; padding: 5px 0;
  border-radius: 6px; color: var(--t-ink-2); cursor: pointer;
  transition: background-color var(--t-fast) ease-out, color var(--t-fast) ease-out;
}
.section-seg button[aria-selected='true'] {
  background: var(--t-surface); color: var(--t-ink); font-weight: 600;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.08);
}
.rail-button--sidebar { display: none; }

.conversation-search {
  margin: 10px 12px 6px;
  border: 0; background: var(--t-chip); border-radius: 999px;
  padding: 7px 14px; font-size: 13px;
}
.conversation-search:focus-visible { box-shadow: 0 0 0 3px var(--t-accent-soft); }

.conversation-list { overflow-y: auto; flex: 1; padding: 4px 8px 8px; display: flex; flex-direction: column; gap: 2px; }
.conversation-item {
  display: flex; align-items: center; gap: 10px; text-align: left;
  padding: 8px 10px; border-radius: 10px; border: 0; background: transparent; cursor: pointer;
  transition: background-color var(--t-fast) ease-out;
}
.conversation-item:hover { background: var(--t-chip); }
.conversation-item--active, .conversation-item--active:hover { background: var(--t-accent); color: #fff; }
.conversation-item--active .conversation-copy small { color: rgba(255, 255, 255, 0.75); }
.conversation-item--active .unread-badge { background: #fff; color: var(--t-accent); }
.conversation-icon {
  width: 34px; height: 34px; border-radius: 50%; flex: none;
  display: inline-flex; align-items: center; justify-content: center;
  background: var(--t-chip); color: var(--t-ink-2); font-weight: 600;
}
.conversation-item--active .conversation-icon { background: rgba(255, 255, 255, 0.2); color: #fff; }
.conversation-lock { margin-left: 5px; font-size: 11px; }
.unread-badge {
  background: var(--t-accent); color: #fff; font-size: 11.5px; font-weight: 600;
  min-width: 20px; height: 20px; border-radius: 999px;
  display: inline-flex; align-items: center; justify-content: center; padding: 0 6px; flex: none;
}

/* ---- 聊天列 ---- */
.chat-region { display: flex; flex-direction: column; min-width: 0; position: relative; background: var(--t-surface); }
.chat-header {
  display: flex; align-items: center; gap: 10px;
  padding: 9px 14px; background: var(--t-surface); border-bottom: 1px solid var(--t-line);
  z-index: 2;
}
.chat-header__title { min-width: 0; }
.chat-header h1 { font-size: 14.5px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.header-status { display: flex; align-items: center; gap: 6px; font-size: 12px; color: var(--t-ink-2); flex-wrap: wrap; }
.status-dot { width: 7px; height: 7px; border-radius: 50%; background: var(--t-online); flex: none; }
.status-dot--busy {
  width: 12px; height: 12px; background: transparent;
  border: 1.6px solid var(--t-accent-soft); border-top-color: var(--t-accent);
  animation: t-spin 0.8s linear infinite;
}
.e2ee-chip {
  display: inline-flex; align-items: center; gap: 3px;
  background: var(--t-accent-soft); color: var(--t-accent);
  font-size: 11px; font-weight: 600; border-radius: 999px; padding: 1px 8px;
}
.chat-header__actions { display: flex; align-items: center; gap: 8px; margin-left: auto; }
.history-search { display: flex; align-items: center; gap: 4px; }
.history-search input { background: var(--t-chip); border: 0; border-radius: 999px; padding: 6px 12px; font-size: 12.5px; width: 150px; }
.history-search button { border: 0; background: transparent; color: var(--t-ink-2); cursor: pointer; font-size: 12px; }
.header-action-button { padding: 6px 11px; font-size: 12.5px; font-weight: 500; }
.member-toggle {
  display: inline-flex; align-items: center; gap: 7px;
  border: 1px solid var(--t-line); background: var(--t-surface); border-radius: 8px;
  padding: 6px 10px; font-size: 12.5px; cursor: pointer;
  transition: background-color var(--t-fast) ease-out;
}
.member-toggle:hover { background: var(--t-chip); }

/* ---- 时间线：壁纸 + 日期胶囊 + 气泡 ---- */
.message-timeline-shell { flex: 1; min-height: 0; display: flex; flex-direction: column; position: relative; }
.message-timeline {
  flex: 1; overflow-y: auto; padding: 18px 22px 10px;
  background-color: var(--t-wall);
  background-image:
    radial-gradient(circle at 12px 12px, rgba(47, 123, 229, 0.09) 2px, transparent 2.6px),
    radial-gradient(circle at 34px 30px, rgba(0, 0, 0, 0.04) 1.6px, transparent 2.1px);
  background-size: 46px 46px, 46px 46px;
  display: flex; flex-direction: column; gap: 3px;
}
.date-pill {
  align-self: center;
  background: rgba(255, 255, 255, 0.92); color: var(--t-ink-2);
  font-size: 12px; padding: 3px 12px; border-radius: 999px;
  box-shadow: var(--t-bubble-shadow); margin: 6px 0 10px;
}
.history-loading {
  align-self: center; display: flex; align-items: center; gap: 7px;
  color: var(--t-ink-2); font-size: 12px; padding: 4px 0;
}
.history-loading::before {
  content: ''; width: 14px; height: 14px; border-radius: 50%;
  border: 1.8px solid var(--t-accent-soft); border-top-color: var(--t-accent);
  animation: t-spin 0.8s linear infinite;
}

.message { display: flex; gap: 8px; max-width: 78%; }
.message--self { align-self: flex-end; flex-direction: row-reverse; }
.message + .message { margin-top: 6px; }
.message--grouped { margin-top: 2px; }
.message-avatar {
  width: 30px; height: 30px; border-radius: 50%; margin-top: 20px; flex: none;
  display: inline-flex; align-items: center; justify-content: center;
  background: var(--t-chip); color: var(--t-ink-2); font-size: 12px; font-weight: 600;
}
.message--grouped .message-avatar { visibility: hidden; }
.message-cluster { min-width: 0; display: flex; flex-direction: column; }
.message-who { font-size: 12.5px; font-weight: 600; color: var(--t-accent); margin-bottom: 1px; }
.message-bubble {
  background: var(--t-bubble-in); border-radius: var(--t-bubble-radius);
  padding: 6px 11px 5px; box-shadow: var(--t-bubble-shadow);
  font-size: 13.5px; line-height: 1.5; min-width: 0;
}
.message--peer .message-bubble { border-top-left-radius: 3px; }
.message--peer.message--grouped .message-bubble { border-top-left-radius: var(--t-bubble-radius); }
.message--self .message-bubble { background: var(--t-bubble-out); border-top-right-radius: 3px; }
.message--self.message--grouped .message-bubble { border-top-right-radius: var(--t-bubble-radius); }
.message-meta {
  display: flex; justify-content: flex-end; align-items: center; gap: 4px;
  font-size: 11px; color: var(--t-ink-3); margin-top: 2px;
}
.message-delivery { letter-spacing: -2px; font-size: 12px; color: var(--t-ink-3); }
.message-delivery--delivered { color: var(--t-accent); }
.message-delivery--failed { color: var(--t-danger); letter-spacing: 0; }
.message-delivery--queued { animation: t-pulse 1.4s ease-in-out infinite; letter-spacing: 0; }
.message-quote {
  border-left: 3px solid var(--t-accent); background: rgba(0, 0, 0, 0.045);
  border-radius: 4px; padding: 3px 8px; margin-bottom: 4px;
  font-size: 12.5px; color: var(--t-ink-2);
}
.message-content { white-space: pre-wrap; overflow-wrap: anywhere; }
.message-content--wrap { overflow-wrap: anywhere; }
.message-actions { display: flex; gap: 4px; margin-top: 3px; opacity: 0; transition: opacity var(--t-fast) ease-out; }
.message:hover .message-actions, .message-actions:focus-within { opacity: 1; }
.message-actions button {
  border: 0; background: var(--t-surface); color: var(--t-ink-2); font-size: 11.5px;
  padding: 3px 8px; border-radius: 6px; cursor: pointer; box-shadow: var(--t-bubble-shadow);
}
.message-actions button:hover { color: var(--t-accent); }

.message--system { align-self: center; max-width: 90%; margin: 4px 0; }
.message-system-text {
  background: rgba(255, 255, 255, 0.85); color: var(--t-ink-2);
  font-size: 12px; padding: 3px 10px; border-radius: 999px;
}

.empty-state { margin: auto; text-align: center; color: var(--t-ink-3); display: flex; flex-direction: column; gap: 6px; }
.empty-glyph { font-size: 26px; }
.timeline-new-messages {
  position: absolute; bottom: 14px; left: 50%; transform: translateX(-50%);
  border: 0; background: var(--t-surface); color: var(--t-accent);
  font-size: 12.5px; font-weight: 600; border-radius: 999px; padding: 6px 14px;
  cursor: pointer; box-shadow: 0 6px 18px rgba(15, 23, 32, 0.16); z-index: 3;
}

/* ---- 消息内附件卡（规格 §7.4；接收侧命令未对齐前下载钮禁用） ---- */
.message-attachment {
  display: flex; align-items: center; gap: 9px;
  background: var(--t-accent-soft); border-radius: 10px;
  padding: 8px 10px; margin: 2px 0 4px; min-width: 220px;
}
.message-attachment__badge {
  width: 34px; height: 34px; border-radius: 8px; flex: none;
  background: var(--t-surface); color: var(--t-accent);
  font-size: 10.5px; font-weight: 700;
  display: inline-flex; align-items: center; justify-content: center;
}
.message-attachment__copy { flex: 1; min-width: 0; display: flex; flex-direction: column; }
.message-attachment__name { font-size: 12.5px; font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.message-attachment__meta { font-size: 11px; color: var(--t-ink-2); display: flex; gap: 6px; align-items: center; flex-wrap: wrap; }
.message-attachment__download {
  width: 30px; height: 30px; border-radius: 50%; border: 0; flex: none;
  background: var(--t-accent); color: #fff; cursor: pointer; font-size: 13px;
}
.message-attachment__download:disabled { opacity: 0.45; cursor: not-allowed; }
.message-attachment--expired { background: var(--t-chip); }
.message-attachment--expired .message-attachment__meta { color: var(--t-ink-3); }
.message-attachment--invalid { background: rgba(201, 59, 59, 0.12); }

/* ---- 输入区 + 上传预览卡 ---- */
.composer-area { padding: 0 14px 12px; background: var(--t-surface); position: relative; }
.composer-quote {
  display: flex; align-items: center; justify-content: space-between; gap: 8px;
  background: var(--t-chip); border-left: 3px solid var(--t-accent); border-radius: 8px;
  padding: 6px 10px; margin-top: 8px;
}
.composer-quote__text {
  font-size: 12.5px; color: var(--t-ink-2);
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
}
.composer-quote__remove { border: 0; background: transparent; color: var(--t-ink-2); cursor: pointer; font-size: 15px; }

.upload-cards { display: flex; flex-direction: column; gap: 8px; margin-top: 8px; }
.upload-card {
  display: flex; gap: 10px; align-items: flex-start;
  border: 1px solid var(--t-line); border-radius: 10px; background: var(--t-surface);
  padding: 9px 11px;
}
.upload-card--failed { border-color: rgba(201, 59, 59, 0.45); }
.upload-card__badge {
  width: 34px; height: 34px; border-radius: 8px; flex: none;
  background: var(--t-accent-soft); color: var(--t-accent);
  display: inline-flex; align-items: center; justify-content: center;
  font-size: 10.5px; font-weight: 700;
}
.upload-card__body { flex: 1; min-width: 0; }
.upload-card__title { font-size: 12.5px; font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.upload-card__meta { font-size: 11.5px; color: var(--t-ink-2); margin-top: 1px; display: flex; align-items: center; gap: 6px; flex-wrap: wrap; }
.upload-card__meta--warn { color: var(--t-warn); }
.upload-card__meta--ok { color: var(--t-online); }
.upload-card__meta--error { color: var(--t-danger); }
.upload-card__actions { display: flex; gap: 6px; margin-top: 6px; }
.upload-card__actions button {
  border: 1px solid var(--t-line); background: var(--t-surface); border-radius: 7px;
  font-size: 11.5px; padding: 3px 9px; cursor: pointer;
}
.upload-card__actions button:hover { background: var(--t-chip); }
.upload-card__remove { border: 0; background: transparent; color: var(--t-ink-3); cursor: pointer; font-size: 14px; flex: none; padding: 0 2px; }

.message-composer {
  display: flex; align-items: flex-end; gap: 8px;
  background: var(--t-chip); border-radius: 999px; padding: 6px 8px 6px 14px; margin-top: 8px;
}
.message-composer textarea {
  flex: 1; border: 0; background: transparent; resize: none; outline: none;
  font-size: 13.5px; padding: 8px 0; max-height: 120px; color: var(--t-ink);
}
.message-composer textarea:focus-visible { box-shadow: none; }
.emoji-button, .attach-button {
  width: 34px; height: 34px; border-radius: 50%; border: 0; background: transparent; flex: none;
  font-size: 16px; color: var(--t-ink-2); cursor: pointer;
  transition: background-color var(--t-fast) ease-out;
}
.emoji-button:hover, .attach-button:hover:not(:disabled) { background: rgba(0, 0, 0, 0.06); }
.attach-button:disabled { opacity: 0.4; cursor: not-allowed; }
.send-button {
  width: 38px; height: 38px; border-radius: 50%; border: 0; flex: none; cursor: pointer;
  background: var(--t-accent); color: #fff; font-size: 16px;
  display: inline-flex; align-items: center; justify-content: center;
  transition: opacity var(--t-fast) ease-out;
}
.send-button:disabled { opacity: 0.5; cursor: not-allowed; }

.emoji-picker {
  position: absolute; right: 14px; bottom: 64px; z-index: 20;
  background: var(--t-surface); border: 1px solid var(--t-line); border-radius: 12px;
  box-shadow: 0 12px 32px rgba(15, 23, 32, 0.16);
  width: 300px; padding: 10px;
}
.emoji-picker__header { display: flex; align-items: center; justify-content: space-between; margin-bottom: 8px; }
.emoji-picker__header button { border: 0; background: transparent; color: var(--t-ink-2); cursor: pointer; font-size: 15px; }
.emoji-picker__tabs { display: flex; gap: 2px; flex-wrap: wrap; margin-bottom: 8px; }
.emoji-picker__tabs button {
  border: 0; background: transparent; font-size: 12px; color: var(--t-ink-2);
  padding: 3px 8px; border-radius: 6px; cursor: pointer;
}
.emoji-picker__tabs button[aria-selected='true'] { background: var(--t-accent-soft); color: var(--t-accent); font-weight: 600; }
.emoji-picker__grid { display: grid; grid-template-columns: repeat(8, 1fr); gap: 2px; max-height: 200px; overflow-y: auto; }
.emoji-picker__grid button { border: 0; background: transparent; font-size: 17px; border-radius: 6px; padding: 3px 0; cursor: pointer; }
.emoji-picker__grid button:hover { background: var(--t-chip); }

/* ---- 成员覆盖抽屉（规格 §4：无背板，translateX 102% → 0，180ms ease-out） ---- */
.member-panel.member-drawer {
  position: absolute; top: 0; right: 0; height: 100%; width: 280px; z-index: 5;
  background: var(--t-surface); border-left: 1px solid var(--t-line);
  box-shadow: var(--t-drawer-shadow);
  transform: translateX(102%);
  transition: transform 180ms ease-out;
  padding: 12px 10px;
}
.member-panel.member-drawer--open { transform: translateX(0); }
.member-list { overflow-y: auto; flex: 1; display: flex; flex-direction: column; gap: 2px; }
.member-row { display: flex; align-items: center; gap: 9px; padding: 5px 8px; border-radius: 8px; }
.member-row:hover { background: var(--t-chip); }
.member-profile-button { border: 0; background: transparent; padding: 0; cursor: pointer; }
.member-admin-badge {
  font-size: 10.5px; color: var(--t-accent); background: var(--t-accent-soft);
  border-radius: 4px; padding: 1px 5px; font-weight: 600;
}
.member-direct-button {
  border: 0; background: transparent; color: var(--t-accent);
  font-size: 12px; cursor: pointer; padding: 4px 6px; border-radius: 6px;
}
.member-direct-button:hover { background: var(--t-accent-soft); }
.member-count { font-size: 12px; color: var(--t-ink-2); }

/* ---- 固定身份卡 ---- */
.identity-card--fixed {
  position: fixed; left: 8px; bottom: 8px; z-index: 6;
  display: inline-flex; align-items: center; gap: 8px;
  background: var(--t-surface); border: 1px solid var(--t-line); border-radius: 999px;
  box-shadow: var(--t-bubble-shadow); padding: 5px 12px 5px 6px;
}
.identity-card .avatar { width: 26px; height: 26px; font-size: 12px; }
.identity-status { width: 8px; height: 8px; border-radius: 50%; background: var(--t-online); }

/* ---- 响应式：窄屏侧栏收窄；成员抽屉本来就覆盖，不占网格 ---- */
@media (max-width: 960px) {
  .workspace-shell { grid-template-columns: 260px minmax(0, 1fr); }
  .rail-button--sidebar { display: inline-flex; }
}
```

- [ ] **Step 2: 验证**

Run: `cd frontend && pnpm exec vitest run && pnpm exec tsc --noEmit && pnpm build`
Expected: 全绿 + 构建通过（此时旧 JSX + 新 CSS：布局仍是四栏网格，成员面板仍占位——抽屉类名在 Task 8 才接线，属预期中间态）。

- [ ] **Step 3: 提交**

```bash
git add frontend/src/styles/workspace.css
git commit -m "feat(styles): t3 workspace layer with bubbles, wallpaper and drawer" -- frontend/src/styles/workspace.css
```

### Task 7: ConversationSidebar 吸收 rail（brand + 分段控件）并删除 WorkspaceRail

**Files:**
- Modify: `frontend/src/components/ConversationSidebar.tsx`（整文件替换）
- Modify: `frontend/src/app/WorkspacePage.tsx`（import 重定向 + 删除 rail JSX + 补 props）
- Modify: `frontend/src/app/WorkspacePage.test.tsx`（4 处断言更新）
- Create: `frontend/src/components/ConversationSidebar.test.tsx`
- Delete: `frontend/src/components/WorkspaceRail.tsx`、`frontend/src/components/WorkspaceRail.test.tsx`

- [ ] **Step 1: 写失败测试**

创建 `frontend/src/components/ConversationSidebar.test.tsx`：

```tsx
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { createFakeBridge } from '../bridge/chatBridge';
import type { BridgeState } from '../bridge/types';
import { ConversationSidebar } from './ConversationSidebar';

const sidebarState: BridgeState = {
  schemaVersion: 1,
  connection: { phase: 'connected', statusText: 'Connected', retryable: false },
  identity: { displayName: 'Alice', userCode: 'A001', admin: false },
  navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
  rooms: [
    { roomName: 'lobby', memberCount: 2, unreadCount: 3, mls: true },
    { roomName: 'study', memberCount: 4, unreadCount: 0 }
  ],
  directMessages: [{ displayName: 'Bob', userCode: 'B002', unreadCount: 1 }],
  activeMessages: [], members: [],
  permissions: { activeRoomCanManage: false },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' }
};

describe('ConversationSidebar', () => {
  afterEach(cleanup);

  it('keeps heading actions, search and the room list reachable', () => {
    const bridge = createFakeBridge(sidebarState);
    const onCreateRoom = vi.fn();

    render(<ConversationSidebar state={sidebarState} bridge={bridge} section="rooms" onCreateRoom={onCreateRoom} />);

    expect(screen.getByRole('img', { name: 'LAN Chat 猫咪标识' })).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: '新建频道' }));
    expect(onCreateRoom).toHaveBeenCalledOnce();
    expect(screen.getByRole('searchbox', { name: '搜索群聊' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'room-lobby' })).toBeInTheDocument();
  });

  it('switches sections through the segmented control with aria-selected', () => {
    const bridge = createFakeBridge(sidebarState);
    const onSectionChange = vi.fn();

    render(<ConversationSidebar state={sidebarState} bridge={bridge} section="rooms" onSectionChange={onSectionChange} onCreateRoom={vi.fn()} />);

    expect(screen.getByRole('tab', { name: '群' })).toHaveAttribute('aria-selected', 'true');
    expect(screen.getByRole('tab', { name: '私' })).toHaveAttribute('aria-selected', 'false');
    fireEvent.click(screen.getByRole('tab', { name: '私' }));
    expect(onSectionChange).toHaveBeenCalledWith('direct');
  });

  it('marks mls rooms with the e2ee lock badge', () => {
    const bridge = createFakeBridge(sidebarState);

    render(<ConversationSidebar state={sidebarState} bridge={bridge} section="rooms" onCreateRoom={vi.fn()} />);

    expect(screen.getByLabelText('lobby 端到端加密')).toBeInTheDocument();
    expect(screen.queryByLabelText('study 端到端加密')).not.toBeInTheDocument();
  });

  it('hides the compact navigation and settings buttons when no handlers are given', () => {
    const bridge = createFakeBridge(sidebarState);

    render(<ConversationSidebar state={sidebarState} bridge={bridge} section="rooms" onCreateRoom={vi.fn()} />);

    expect(screen.queryByRole('button', { name: '打开导航' })).not.toBeInTheDocument();
    expect(screen.queryByRole('button', { name: '设置' })).not.toBeInTheDocument();
  });
});
```

- [ ] **Step 2: 运行确认失败**

Run: `cd frontend && pnpm exec vitest run src/components/ConversationSidebar.test.tsx`
Expected: FAIL —— 找不到 `role="img"`（LAN Chat 猫咪标识）等断言。

- [ ] **Step 3: 重写 ConversationSidebar.tsx**

`frontend/src/components/ConversationSidebar.tsx` 全文替换为：

```tsx
import { createCommand } from '../bridge/chatBridge';
import { useState } from 'react';
import type { BridgeState, ChatBridgeClient } from '../bridge/types';
import catBrandMark from '../assets/lan-chat-cat.png';

export type WorkspaceSection = 'rooms' | 'direct';

type ConversationSidebarProps = {
  state: BridgeState;
  bridge: ChatBridgeClient;
  section: WorkspaceSection;
  onSectionChange?: (section: WorkspaceSection) => void;
  onCreateRoom(): void;
  onSettings?: () => void;
  onOpenSidebar?: () => void;
};

export function ConversationSidebar({ state, bridge, section, onSectionChange, onCreateRoom, onSettings, onOpenSidebar }: ConversationSidebarProps) {
  const active = state.navigation.activeConversation;
  const showRooms = section === 'rooms';
  const [query, setQuery] = useState('');
  const normalizedQuery = query.trim().toLowerCase();
  const rooms = state.rooms.filter((room) => room.roomName.toLowerCase().includes(normalizedQuery));
  const directMessages = state.directMessages.filter((direct) => `${direct.displayName} ${direct.userCode}`.toLowerCase().includes(normalizedQuery));

  return (
    <aside className="conversation-sidebar" data-testid="conversation-sidebar">
      <div className="sidebar-top">
        <img className="brand-mark" src={catBrandMark} alt="LAN Chat 猫咪标识" />
        <div className="section-seg" role="tablist" aria-label="会话分区">
          <button type="button" role="tab" aria-label="群" title="群聊" aria-selected={showRooms} onClick={() => onSectionChange?.('rooms')}>群</button>
          <button type="button" role="tab" aria-label="私" title="私信" aria-selected={!showRooms} onClick={() => onSectionChange?.('direct')}>私</button>
        </div>
        {onOpenSidebar && <button className="icon-button rail-button--sidebar" type="button" aria-label="打开导航" title="打开导航" onClick={onOpenSidebar}>☰</button>}
        {onSettings && <button className="icon-button" type="button" aria-label="设置" title="设置" onClick={onSettings}>⚙</button>}
      </div>
      <div className="sidebar-heading">
        <div>
          <p className="eyebrow">{showRooms ? '群聊' : '私信'}</p>
          <h2>{showRooms ? '频道' : '消息'}</h2>
        </div>
        <div className="sidebar-actions">
          {showRooms ? <button className="icon-button" type="button" aria-label="新建频道" title="新建频道" onClick={onCreateRoom}>+</button> : null}
          <button className="icon-button" type="button" aria-label={showRooms ? '刷新频道' : '刷新私信'} title={showRooms ? '刷新频道' : '刷新私信'}
            onClick={() => bridge.dispatch(createCommand(showRooms ? 'directory.refreshRooms' : 'directory.refreshUsers', {}))}>↻</button>
        </div>
      </div>
      <input className="conversation-search" type="search" aria-label={showRooms ? '搜索群聊' : '搜索私信'} placeholder={showRooms ? '搜索频道…' : '搜索私信…'} value={query} onChange={(event) => setQuery(event.target.value)} />
      {showRooms ? <div className="conversation-list" aria-label="群聊频道列表">
        {rooms.map((room) => (
          <button className={`conversation-item ${active?.kind === 'room' && active.id === room.roomName ? 'conversation-item--active' : ''}`} key={room.roomName} type="button" aria-label={`room-${room.roomName}`} aria-current={active?.kind === 'room' && active.id === room.roomName ? 'page' : undefined} onClick={() => bridge.dispatch(createCommand('conversation.selectRoom', { room: room.roomName }))}>
            <span className="conversation-icon">#</span>
            <span className="conversation-copy"><strong>{room.roomName}{room.mls && <span className="conversation-lock" title="端到端加密" aria-label={`${room.roomName} 端到端加密`}>🔒</span>}</strong><small>{room.memberCount} 人</small></span>
            {room.unreadCount > 0 && <span className="unread-badge">{room.unreadCount}</span>}
          </button>
        ))}
      </div> : <div className="conversation-list" aria-label="私信列表">
        {directMessages.map((direct) => (
          <button className={`conversation-item ${active?.kind === 'dm' && active.userCode === direct.userCode ? 'conversation-item--active' : ''}`} key={direct.userCode} type="button" aria-label={`direct-${direct.displayName.toLowerCase()}`} aria-current={active?.kind === 'dm' && active.userCode === direct.userCode ? 'page' : undefined} onClick={() => bridge.dispatch(createCommand('conversation.selectDirect', { userCode: direct.userCode }))}>
            <span className="avatar avatar--small">{direct.displayName.slice(0, 1)}</span>
            <span className="conversation-copy"><strong>{direct.displayName}</strong><small>#{direct.userCode}</small></span>
            {direct.unreadCount > 0 && <span className="unread-badge">{direct.unreadCount}</span>}
          </button>
        ))}
      </div>}
    </aside>
  );
}
```

（对比旧文件的变化：`WorkspaceSection` 类型迁入本文件并 export；新增 `sidebar-top`（brand + seg + ☰ + ⚙，后两者按 props 可选）；房间名后可挂 `conversation-lock`；移除已无消费者引用的 `data-motion` 属性；列表/搜索/heading 行为不变。）

- [ ] **Step 4: WorkspacePage.tsx 接线**

`frontend/src/app/WorkspacePage.tsx`：

(a) 删除这两行 import：

```tsx
import { WorkspaceRail } from '../components/WorkspaceRail';
import type { WorkspaceSection } from '../components/WorkspaceRail';
```

在 `import { ConversationSidebar } ...` 行之后加：

```tsx
import type { WorkspaceSection } from '../components/ConversationSidebar';
```

(b) 把这两行 JSX：

```tsx
      <WorkspaceRail section={section} onSectionChange={setSection} onSettings={onSettings} onOpenSidebar={() => setSidebarOpen(true)} />
      <ConversationSidebar bridge={bridge} state={state} section={section} onCreateRoom={openCreateRoom} />
```

替换为：

```tsx
      <ConversationSidebar bridge={bridge} state={state} section={section} onSectionChange={setSection} onCreateRoom={openCreateRoom} onSettings={onSettings} onOpenSidebar={() => setSidebarOpen(true)} />
```

(c) `sidebarOpen` ModalSurface 里的 ConversationSidebar（约第 218 行）改为（补 `onSectionChange`）：

```tsx
      {sidebarOpen && <ModalSurface title={section === 'rooms' ? '群聊导航' : '私信导航'} onClose={() => setSidebarOpen(false)}><ConversationSidebar bridge={bridge} state={state} section={section} onSectionChange={setSection} onCreateRoom={openCreateRoom} /></ModalSurface>}
```

- [ ] **Step 5: WorkspacePage.test.tsx 四处更新**

`frontend/src/app/WorkspacePage.test.tsx`：

(a) 第 45-46 行：

```tsx
    expect(screen.getByTestId('workspace-rail')).toBeInTheDocument();
    expect(screen.getByTestId('conversation-sidebar')).toBeInTheDocument();
```

改为：

```tsx
    expect(screen.getByTestId('conversation-sidebar')).toBeInTheDocument();
    expect(screen.getByRole('img', { name: 'LAN Chat 猫咪标识' })).toBeInTheDocument();
    expect(screen.getByRole('tab', { name: '群' })).toHaveAttribute('aria-selected', 'true');
```

(b) 第 80 行（前后文是 room-study 与 direct-bob 两行）：

```tsx
    fireEvent.click(screen.getByRole('button', { name: 'room-study' }));
    fireEvent.click(screen.getByRole('button', { name: '私' }));
    fireEvent.click(screen.getByRole('button', { name: 'direct-bob' }));
```

改为：

```tsx
    fireEvent.click(screen.getByRole('button', { name: 'room-study' }));
    fireEvent.click(screen.getByRole('tab', { name: '私' }));
    fireEvent.click(screen.getByRole('button', { name: 'direct-bob' }));
```

(c) 第 97 行（前后文是 `expect(screen.queryByRole('button', { name: 'direct-bob' })).not.toBeInTheDocument();`）：

```tsx
    fireEvent.click(screen.getByRole('tab', { name: '私' }));
```

(d) 第 108 行：

```tsx
    fireEvent.click(screen.getByRole('button', { name: '群' }));
```

改为：

```tsx
    fireEvent.click(screen.getByRole('tab', { name: '群' }));
```

- [ ] **Step 6: 删除 WorkspaceRail 并验证**

```bash
git rm frontend/src/components/WorkspaceRail.tsx frontend/src/components/WorkspaceRail.test.tsx
```

Run: `cd frontend && pnpm exec vitest run && pnpm exec tsc --noEmit`
Expected: 全绿（WorkspaceRail.test 的 aria-pressed 用例随文件删除；其余用例不回归）。

- [ ] **Step 7: 提交**

```bash
git add frontend/src/components/ConversationSidebar.tsx frontend/src/components/ConversationSidebar.test.tsx frontend/src/app/WorkspacePage.tsx frontend/src/app/WorkspacePage.test.tsx
git commit -m "refactor(workspace): merge the rail into the conversation sidebar" -- frontend/src/components/ConversationSidebar.tsx frontend/src/components/ConversationSidebar.test.tsx frontend/src/app/WorkspacePage.tsx frontend/src/app/WorkspacePage.test.tsx frontend/src/components/WorkspaceRail.tsx frontend/src/components/WorkspaceRail.test.tsx
```

---

### Task 8: 成员覆盖抽屉（常驻 MemberPanel + member-drawer 类）

**Files:**
- Modify: `frontend/src/app/WorkspacePage.tsx`（两处）
- Modify: `frontend/src/app/WorkspacePage.test.tsx`（三个用例）

- [ ] **Step 1: 更新测试（先红）**

`frontend/src/app/WorkspacePage.test.tsx`：

(a) 用例 `opens the member drawer from the member action`（约第 122-131 行），把：

```tsx
    fireEvent.click(screen.getByRole('button', { name: 'members-toggle' }));

    const dialog = screen.getByRole('dialog', { name: '成员' });
    expect(dialog).toBeInTheDocument();
    expect(within(dialog).getByText('Bob')).toBeInTheDocument();
```

改为：

```tsx
    fireEvent.click(screen.getByRole('button', { name: 'members-toggle' }));

    const drawer = screen.getByTestId('member-panel');
    expect(drawer).toHaveClass('member-drawer--open');
    expect(within(drawer).getByText('Bob')).toBeInTheDocument();
```

(b) 用例 `opens a private conversation for another member but not the local member`（约第 133-146 行），把：

```tsx
    const dialog = screen.getByRole('dialog', { name: '成员' });
    expect(within(dialog).queryByRole('button', { name: 'direct-Alice' })).not.toBeInTheDocument();
    fireEvent.click(within(dialog).getByRole('button', { name: 'direct-Bob' }));
```

改为：

```tsx
    const drawer = screen.getByTestId('member-panel');
    expect(within(drawer).queryByRole('button', { name: 'direct-Alice' })).not.toBeInTheDocument();
    fireEvent.click(within(drawer).getByRole('button', { name: 'direct-Bob' }));
```

(c) 用例 `closes the member drawer with Escape`（约第 200-208 行），把：

```tsx
    fireEvent.click(screen.getByRole('button', { name: 'members-toggle' }));
    fireEvent.keyDown(window, { key: 'Escape' });

    expect(screen.queryByRole('dialog', { name: '成员' })).not.toBeInTheDocument();
```

改为：

```tsx
    fireEvent.click(screen.getByRole('button', { name: 'members-toggle' }));
    expect(screen.getByTestId('member-panel')).toHaveClass('member-drawer--open');
    fireEvent.keyDown(window, { key: 'Escape' });

    expect(screen.getByTestId('member-panel')).not.toHaveClass('member-drawer--open');
```

- [ ] **Step 2: 运行确认失败**

Run: `cd frontend && pnpm exec vitest run src/app/WorkspacePage.test.tsx`
Expected: FAIL —— 点开后 member-panel 没有 `member-drawer--open`（当前走 ModalSurface）。

- [ ] **Step 3: WorkspacePage.tsx 两处修改**

(a) 常驻 MemberPanel（约第 216 行）改为挂抽屉类：

```tsx
      <MemberPanel state={state} className={`member-drawer${memberDrawerOpen ? ' member-drawer--open' : ''}`} localUserCode={state.identity.userCode} onStartDirect={startDirect} onViewProfile={setMemberProfile} canManage={canManageRoom} onManageMember={(member) => { setAdminFeedback({ status: 'idle', message: '' }); setMemberToManage(member); }} />
```

(b) 整行删除成员 ModalSurface（约第 219 行）：

```tsx
      {memberDrawerOpen && <ModalSurface title="成员" onClose={() => setMemberDrawerOpen(false)}><MemberPanel state={state} className="member-panel--modal" localUserCode={state.identity.userCode} onStartDirect={startDirect} onViewProfile={setMemberProfile} canManage={canManageRoom} onManageMember={(member) => { setAdminFeedback({ status: 'idle', message: '' }); setMemberToManage(member); }} /></ModalSurface>}
```

（`memberDrawerOpen` state 与 Escape 关闭逻辑保留，驱动抽屉类名；ModalSurface import 仍被其他弹窗使用，不删。）

- [ ] **Step 4: 运行确认通过**

Run: `cd frontend && pnpm exec vitest run src/app/WorkspacePage.test.tsx && pnpm exec tsc --noEmit`
Expected: 全绿。

- [ ] **Step 5: 提交**

```bash
git add frontend/src/app/WorkspacePage.tsx frontend/src/app/WorkspacePage.test.tsx
git commit -m "feat(workspace): overlay member drawer on the chat column" -- frontend/src/app/WorkspacePage.tsx frontend/src/app/WorkspacePage.test.tsx
```

---

### Task 9: ChatHeader 连接 spinner + MLS 端到端加密 chip

**Files:**
- Modify: `frontend/src/components/ChatHeader.tsx`（整文件替换）
- Create: `frontend/src/components/ChatHeader.test.tsx`

- [ ] **Step 1: 写失败测试**

创建 `frontend/src/components/ChatHeader.test.tsx`：

```tsx
import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import type { BridgeState } from '../bridge/types';
import { ChatHeader } from './ChatHeader';

function headerState(overrides: Partial<BridgeState> = {}): BridgeState {
  return {
    schemaVersion: 1,
    connection: { phase: 'connected', statusText: '已连接', retryable: false },
    identity: { displayName: 'Alice', userCode: 'A001', admin: false },
    navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
    rooms: [], directMessages: [], activeMessages: [],
    members: [{ displayName: 'Alice', userCode: 'A001', online: true, admin: false }],
    permissions: { activeRoomCanManage: false },
    savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' },
    ...overrides
  };
}

describe('ChatHeader', () => {
  afterEach(cleanup);

  it('shows a busy status dot and the retry counter while reconnecting', () => {
    render(<ChatHeader state={headerState({
      connection: { phase: 'reconnecting', statusText: '重连中', retryable: true, reconnectAttempt: 3 }
    })} onMembers={() => undefined} />);

    expect(screen.getByText('重连中 · 第 3 次重连')).toBeInTheDocument();
    expect(document.querySelector('.status-dot--busy')).not.toBeNull();
  });

  it('marks mls conversations with the e2ee chip and omits it otherwise', () => {
    const { rerender } = render(<ChatHeader state={headerState({
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby', mls: true } }
    })} onMembers={() => undefined} />);

    expect(screen.getByText('🔒 端到端加密')).toBeInTheDocument();

    rerender(<ChatHeader state={headerState()} onMembers={() => undefined} />);
    expect(screen.queryByText('🔒 端到端加密')).not.toBeInTheDocument();
  });
});
```

- [ ] **Step 2: 运行确认失败**

Run: `cd frontend && pnpm exec vitest run src/components/ChatHeader.test.tsx`
Expected: FAIL —— 重连文案与 chip 均未出现。

- [ ] **Step 3: 重写 ChatHeader.tsx**

`frontend/src/components/ChatHeader.tsx` 全文替换为：

```tsx
import type { BridgeState } from '../bridge/types';

type ChatHeaderProps = {
  state: BridgeState;
  onMembers: () => void;
  canManage?: boolean;
  onManageRoom?: () => void;
  onConnectionApprovals?: () => void;
  historyQuery?: string;
  onHistoryQueryChange?: (query: string) => void;
};

export function ChatHeader({ state, onMembers, canManage = false, onManageRoom, onConnectionApprovals, historyQuery = '', onHistoryQueryChange }: ChatHeaderProps) {
  const conversation = state.navigation.activeConversation;
  const onlineCount = state.members.filter((member) => member.online).length;
  const approvalCount = state.connectionApprovals?.length ?? 0;
  const connecting = state.connection.phase === 'connecting' || state.connection.phase === 'reconnecting';
  const reconnectAttempt = state.connection.reconnectAttempt;
  return (
    <header className="chat-header">
      <div className="chat-header__title">
        <h1>{conversation?.kind === 'room' ? `# ${conversation.title}` : conversation?.title ?? '会话'}</h1>
        <p className="header-status">
          <span className={`status-dot${connecting ? ' status-dot--busy' : ''}`} aria-hidden="true" />
          <span>{state.connection.statusText}{connecting && reconnectAttempt ? ` · 第 ${reconnectAttempt} 次重连` : ''}</span>
          {conversation?.mls && <span className="e2ee-chip" title="该会话通过 MLS 端到端加密">🔒 端到端加密</span>}
        </p>
      </div>
      <div className="chat-header__actions">
        {onHistoryQueryChange && <label className="history-search">
          <input aria-label="搜索当前会话" value={historyQuery} onChange={(event) => onHistoryQueryChange(event.target.value)} placeholder="搜索消息" />
          {historyQuery && <button type="button" aria-label="清除消息搜索" onClick={() => onHistoryQueryChange('')}>清除</button>}
        </label>}
        {canManage && <button className="secondary-button header-action-button" type="button" aria-label="频道管理" onClick={onManageRoom}>频道管理</button>}
        {state.identity.admin && <button className="secondary-button header-action-button" type="button" aria-label="连接审批" onClick={onConnectionApprovals}>连接审批{approvalCount > 0 ? ` (${approvalCount})` : ''}</button>}
        <button className="member-toggle" type="button" aria-label="members-toggle" onClick={onMembers}>
          <span className="avatar-stack"><span className="avatar avatar--tiny">A</span><span className="avatar avatar--tiny avatar--offset">B</span></span>
          <span>{onlineCount} 名在线成员</span>
        </button>
      </div>
    </header>
  );
}
```

（对比旧文件的变化：删掉 `当前会话` eyebrow（无测试引用）；标题包一层 `chat-header__title`；状态行改 flex 结构并加 busy spinner 与重连计数；MLS chip 只在 `conversation.mls` 为 true 时渲染——该字段由 Task 1 加入，C++ 未发布前不显示。actions 块逐字未动。）

- [ ] **Step 4: 运行确认通过**

Run: `cd frontend && pnpm exec vitest run src/components/ChatHeader.test.tsx src/app/WorkspacePage.test.tsx && pnpm exec tsc --noEmit`
Expected: 全绿。

- [ ] **Step 5: 提交**

```bash
git add frontend/src/components/ChatHeader.tsx frontend/src/components/ChatHeader.test.tsx
git commit -m "feat(header): reconnect spinner and mls e2ee chip" -- frontend/src/components/ChatHeader.tsx frontend/src/components/ChatHeader.test.tsx
```

---

### Task 10: MessageTimeline 分组气泡、日期胶囊与历史加载指示

**Files:**
- Modify: `frontend/src/components/MessageTimeline.tsx`
- Test: `frontend/src/components/MessageTimeline.test.tsx`（追加 3 个用例）

- [ ] **Step 1: 写失败测试（追加用例）**

在 `frontend/src/components/MessageTimeline.test.tsx` 的 `describe('MessageTimeline motion budget', ...)` 内追加：

```tsx
  it('groups consecutive messages from the same author', () => {
    const state = {
      ...baseState,
      activeMessages: [receivedMessage('history-1'), { ...receivedMessage('history-2'), time: '10:02' }]
    };

    render(<MessageTimeline bridge={createFakeBridge(state)} state={state} />);

    expect(screen.getByTestId('message-history-1')).not.toHaveClass('message--grouped');
    expect(screen.getByTestId('message-history-2')).toHaveClass('message--grouped');
  });

  it('shows the date pill above the history window', () => {
    const state = { ...baseState, activeMessages: [message('history-1')] };

    render(<MessageTimeline bridge={createFakeBridge(state)} state={state} />);

    expect(document.querySelector('.date-pill')).not.toBeNull();
  });

  it('clears the history loading hint once the older page renders', () => {
    const state = {
      ...baseState,
      activeMessages: Array.from({ length: 600 }, (_, index) => message(`history-${index}`))
    };
    render(<MessageTimeline bridge={createFakeBridge(state)} state={state} />);
    const timeline = screen.getByTestId('message-timeline');
    setScrollableGeometry(timeline, 0);

    fireEvent.scroll(timeline);

    expect(screen.getByTestId('message-history-499')).toBeInTheDocument();
    expect(screen.queryByRole('status')).toBeNull();
  });
```

- [ ] **Step 2: 运行确认失败**

Run: `cd frontend && pnpm exec vitest run src/components/MessageTimeline.test.tsx`
Expected: FAIL —— 无 `message--grouped`、无 `.date-pill`。

- [ ] **Step 3: MessageTimeline.tsx 三处修改**

(a) `const [newMessageCount, setNewMessageCount] = useState(0);` 之后加：

```tsx
  const [historyLoading, setHistoryLoading] = useState(false);
```

(b) `handleScroll` 的翻页分支改为（加一行 `setHistoryLoading(true);`）：

```tsx
    if (timeline.scrollTop <= 0 && visibleWindowStart > 0) {
      pendingHistoryPageRef.current = true;
      setHistoryLoading(true);
      setWindowStart(Math.max(0, visibleWindowStart - historyWindowSize));
      return;
    }
```

(c) `useLayoutEffect` 的 `pagedBack` 分支改为（加一行 `setHistoryLoading(false);`）：

```tsx
      if (pagedBack) {
        const timeline = timelineRef.current;
        if (timeline) timeline.scrollTop = timeline.scrollHeight;
        atBottomRef.current = true;
        setNewMessageCount(0);
        setHistoryLoading(false);
      } else {
```

(d) JSX 渲染段（`{messages.length === 0 ? (...) : messages.map(...)}`）改为：

```tsx
        {messages.length === 0 ? (
          <div className="empty-state"><span className="empty-glyph">✦</span><p>暂无消息，开始聊天吧。</p></div>
        ) : <>
          <div className="date-pill" aria-hidden="true">今天</div>
          {historyLoading && <div className="history-loading" role="status">正在加载更早的消息…</div>}
          {messages.map((message, index) => {
            const previous = index > 0 ? messages[index - 1] : undefined;
            const grouped = !!previous && !previous.systemMessage && !message.systemMessage
              && previous.userCode === message.userCode
              && previous.selfMessage === message.selfMessage;
            return <MessageItem key={message.messageId} message={message} bridge={bridge} onQuote={onQuote} canRecall={canRecall?.(message)} showTime={settings.showSendTime} animateEntry={enteringMessageIds.has(message.messageId)} grouped={grouped} />;
          })}
        </>}
```

- [ ] **Step 4: 运行确认通过（含 MessageItem 类型预热）**

本任务把 `grouped` 透传给 `MessageItem`，但该 prop 在 Task 11 才全面接线——为让 tsc 通过，现在对 `frontend/src/components/MessageItem.tsx` 做两处最小修改（Task 11 的整文件替换会覆盖它们）：

(a) `MessageItemProps` 里 `bridge: ChatBridgeClient;` 之后加一行：

```tsx
  grouped?: boolean;
```

(b) 把组件函数签名这一行：

```tsx
export const MessageItem = memo(function MessageItem({ message, bridge, onCopy, onQuote, onLocalDelete, onRecall, canRecall = false, showTime = true, animateEntry = true }: MessageItemProps) {
```

改为：

```tsx
export const MessageItem = memo(function MessageItem({ message, bridge, grouped = false, onCopy, onQuote, onLocalDelete, onRecall, canRecall = false, showTime = true, animateEntry = true }: MessageItemProps) {
```

并在 `areMessageItemPropsEqual` 的 `previous.animateEntry === next.animateEntry` 之后加：

```tsx
    && previous.grouped === next.grouped
```

Run: `cd frontend && pnpm exec vitest run src/components/MessageTimeline.test.tsx && pnpm exec tsc --noEmit`
Expected: 全绿。

- [ ] **Step 5: 提交**

```bash
git add frontend/src/components/MessageTimeline.tsx frontend/src/components/MessageTimeline.test.tsx frontend/src/components/MessageItem.tsx
git commit -m "feat(timeline): grouped bubbles, date pill and history loading hint" -- frontend/src/components/MessageTimeline.tsx frontend/src/components/MessageTimeline.test.tsx frontend/src/components/MessageItem.tsx
```

---

### Task 11: MessageItem T3 气泡、✓✓ 回执与消息内附件卡

**Files:**
- Modify: `frontend/src/components/MessageItem.tsx`（整文件替换）
- Test: `frontend/src/components/MessageItem.test.tsx`（追加 4 个用例）

- [ ] **Step 1: 写失败测试（追加用例）**

在 `frontend/src/components/MessageItem.test.tsx` 的 `describe('MessageItem', ...)` 内追加：

```tsx
  it('shows delivery ticks for each delivery state', () => {
    const bridge = createFakeBridge(state);
    const { rerender } = render(<MessageItem message={{ ...message, deliveryState: 'queued' }} bridge={bridge} />);

    let delivery = screen.getByLabelText('投递状态：queued');
    expect(delivery).toHaveClass('message-delivery--queued');
    expect(delivery).toHaveTextContent('⏱ 发送中');

    rerender(<MessageItem message={{ ...message, deliveryState: 'delivered' }} bridge={bridge} />);
    delivery = screen.getByLabelText('投递状态：delivered');
    expect(delivery).toHaveTextContent('✓✓');
    expect(delivery).toHaveClass('message-delivery--delivered');

    rerender(<MessageItem message={{ ...message, deliveryState: 'failed' }} bridge={bridge} />);
    expect(screen.getByLabelText('投递状态：failed')).toHaveTextContent('⚠');
  });

  it('marks consecutive grouped messages and hides the author line', () => {
    const bridge = createFakeBridge(state);

    const { rerender } = render(<MessageItem message={peerMessage} bridge={bridge} />);
    expect(screen.getByText('Bob')).toBeInTheDocument();

    rerender(<MessageItem message={peerMessage} bridge={bridge} grouped />);

    expect(screen.getByTestId('message-m-2')).toHaveClass('message--grouped');
    expect(screen.queryByText('Bob')).not.toBeInTheDocument();
  });

  it('renders an attachment card with name, size, lock and a disabled download', () => {
    const bridge = createFakeBridge(state);
    const attachmentMessage: MessageItemData = {
      ...peerMessage,
      messageId: 'm-file',
      attachment: {
        attachmentId: 'att-1', fileName: 'design.pdf', logicalSize: 3 * 1024 * 1024,
        status: 'available', receivedChunks: 3, totalChunks: 3
      }
    };

    render(<MessageItem message={attachmentMessage} bridge={bridge} />);

    const card = screen.getByTestId('message-attachment-att-1');
    expect(card).toHaveTextContent('design.pdf');
    expect(card).toHaveTextContent('3.0 MiB');
    expect(card).toHaveTextContent('🔒 端到端加密');
    expect(screen.getByRole('button', { name: '下载 design.pdf' })).toBeDisabled();
  });

  it('marks expired attachments and explains the expiry', () => {
    const bridge = createFakeBridge(state);
    const expiredMessage: MessageItemData = {
      ...peerMessage,
      messageId: 'm-expired',
      attachment: { attachmentId: 'att-2', fileName: 'notes.txt', logicalSize: 1024, status: 'expired' }
    };

    render(<MessageItem message={expiredMessage} bridge={bridge} />);

    expect(screen.getByTestId('message-attachment-att-2')).toHaveClass('message-attachment--expired');
    expect(screen.getByText('文件已过期（超过 24 小时），请让发送者重新发送')).toBeInTheDocument();
  });
```

- [ ] **Step 2: 运行确认失败**

Run: `cd frontend && pnpm exec vitest run src/components/MessageItem.test.tsx`
Expected: FAIL —— 无投递刻度、无附件卡。

- [ ] **Step 3: 整文件替换 MessageItem.tsx**

`frontend/src/components/MessageItem.tsx` 全文替换为：

```tsx
import { memo, useEffect, useLayoutEffect, useRef, useState } from 'react';

import type { BridgeCommand, ChatBridgeClient, MessageAttachment, MessageItem as MessageItemData } from '../bridge/types';
import { createCommand } from '../bridge/chatBridge';
import { CommandFeedback } from './CommandFeedback';
import { animateMessage } from '../animation/motion';

type MessageItemProps = {
  message: MessageItemData;
  bridge: ChatBridgeClient;
  grouped?: boolean;
  onCopy?: (message: MessageItemData) => void;
  onQuote?: (message: MessageItemData) => void;
  onLocalDelete?: (message: MessageItemData) => void;
  onRecall?: (message: MessageItemData) => void;
  canRecall?: boolean;
  showTime?: boolean;
  animateEntry?: boolean;
};

const deliveryTicks: Record<NonNullable<MessageItemData['deliveryState']>, string> = {
  queued: '⏱',
  sent: '✓',
  delivered: '✓✓',
  failed: '⚠'
};

export const MessageItem = memo(function MessageItem({ message, bridge, grouped = false, onCopy, onQuote, onLocalDelete, onRecall, canRecall = false, showTime = true, animateEntry = true }: MessageItemProps) {
  const [feedback, setFeedback] = useState<{ status: 'idle' | 'pending' | 'success' | 'error'; message: string }>({ status: 'idle', message: '' });
  const pendingCommandIdRef = useRef<string | null>(null);
  const pendingUnsubscribeRef = useRef<(() => void) | null>(null);
  const messageRef = useRef<HTMLElement>(null);

  useEffect(() => () => pendingUnsubscribeRef.current?.(), []);
  useLayoutEffect(() => animateEntry ? animateMessage(messageRef.current) : undefined, [message.messageId, animateEntry]);

  const [firstLine = '', ...remainingLines] = message.content.split('\n');
  const isQuotedMessage = firstLine.startsWith('> ') && remainingLines.length > 0;
  const quotedText = isQuotedMessage ? firstLine : '';
  const messageText = isQuotedMessage ? remainingLines.join('\n') : message.content;

  const dispatch = (type: string, label = '操作', payload: Record<string, unknown> = { messageId: message.messageId }) => {
    if (pendingCommandIdRef.current) return;
    const command: BridgeCommand = createCommand(type, payload);
    pendingCommandIdRef.current = command.id;
    setFeedback({ status: 'pending', message: `${label}…` });
    const unsubscribe = bridge.subscribeCommandResult((result) => {
      if (result.id !== command.id || pendingCommandIdRef.current !== command.id) return;
      unsubscribe();
      if (pendingUnsubscribeRef.current === unsubscribe) pendingUnsubscribeRef.current = null;
      pendingCommandIdRef.current = null;
      setFeedback(result.ok
        ? { status: 'success', message: `${label}完成。` }
        : { status: 'error', message: result.error?.message ?? `${label}失败。` });
    });
    pendingUnsubscribeRef.current = unsubscribe;
    bridge.dispatch(command);
  };

  return (
    <article ref={messageRef} className={`message ${message.selfMessage ? 'message--self' : 'message--peer'}${grouped ? ' message--grouped' : ''}${message.systemMessage ? ' message--system' : ''}`} data-testid={`message-${message.messageId}`}>
      {!message.systemMessage && !message.selfMessage && <span className="message-avatar" aria-hidden="true">{message.displayName.slice(0, 1)}</span>}
      <div className="message-cluster">
        {message.systemMessage ? <p className="message-system-text" data-testid={`message-content-${message.messageId}`}>{message.content}</p> : <>
          {!grouped && <div className="message-who">{message.displayName}</div>}
          <div className="message-bubble">
            {message.attachment && <AttachmentCard attachment={message.attachment} />}
            {isQuotedMessage && <blockquote className="message-quote message-content--wrap" data-testid={`message-quote-${message.messageId}`}>{quotedText}</blockquote>}
            {(!isQuotedMessage || messageText) && <p className="message-content message-content--wrap" data-testid={`message-content-${message.messageId}`}>{messageText}</p>}
            <div className="message-meta">
              {showTime && <span>{message.time}</span>}
              {message.selfMessage && message.deliveryState && <span className={`message-delivery message-delivery--${message.deliveryState}`} aria-label={`投递状态：${message.deliveryState}`}>{deliveryTicks[message.deliveryState]}{message.deliveryState === 'queued' ? ' 发送中' : ''}</span>}
            </div>
          </div>
          <div className="message-actions" aria-label={`actions-${message.messageId}`}>
            <button type="button" onClick={() => (onCopy ?? (() => dispatch('message.copy', '已复制', { text: message.content })))(message)}>复制</button>
            <button type="button" onClick={() => (onQuote ?? (() => undefined))(message)}>引用</button>
            {message.selfMessage && <>
              {message.deliveryState === 'failed' && <button type="button" onClick={() => dispatch('message.retry')}>重试</button>}
              <button type="button" disabled={pendingCommandIdRef.current !== null} onClick={() => (onLocalDelete ?? (() => dispatch('message.removeLocal', '已在本地删除')))(message)}>删除</button>
            </>}
            {canRecall && <>
              <button type="button" disabled={pendingCommandIdRef.current !== null} onClick={() => (onRecall ?? (() => dispatch('message.recall', '撤回')))(message)}>撤回</button>
            </>}
          </div>
          <CommandFeedback {...feedback} />
        </>}
      </div>
    </article>
  );
}, areMessageItemPropsEqual);

function AttachmentCard({ attachment }: { attachment: MessageAttachment }) {
  const expired = attachment.status === 'expired';
  const invalid = attachment.status === 'verified-failed';
  const extension = attachment.fileName.includes('.')
    ? attachment.fileName.slice(attachment.fileName.lastIndexOf('.') + 1).toUpperCase().slice(0, 4)
    : 'FILE';
  const sizeMiB = attachment.logicalSize > 0 ? `${(attachment.logicalSize / (1024 * 1024)).toFixed(1)} MiB` : '';
  const statusCopy = expired
    ? '文件已过期（超过 24 小时），请让发送者重新发送'
    : invalid
      ? '校验失败，等待发送者重传'
      : attachment.status === 'downloading'
        ? `接收中… ${attachment.receivedChunks ?? 0}${attachment.totalChunks ? ` / ${attachment.totalChunks} 块` : ''}`
        : '';
  return (
    <div className={`message-attachment${expired ? ' message-attachment--expired' : ''}${invalid ? ' message-attachment--invalid' : ''}`} data-testid={`message-attachment-${attachment.attachmentId}`}>
      <span className="message-attachment__badge" aria-hidden="true">{extension}</span>
      <span className="message-attachment__copy">
        <strong className="message-attachment__name">{attachment.fileName}</strong>
        <span className="message-attachment__meta">
          {sizeMiB && <span>{sizeMiB}</span>}
          <span>🔒 端到端加密</span>
          {statusCopy && <span>{statusCopy}</span>}
        </span>
      </span>
      <button className="message-attachment__download" type="button" aria-label={`下载 ${attachment.fileName}`}
        disabled
        title="下载通道联调中（待对齐）">↓</button>
    </div>
  );
}

function areMessageItemPropsEqual(previous: MessageItemProps, next: MessageItemProps) {
  const previousMessage = previous.message;
  const nextMessage = next.message;
  return previous.bridge === next.bridge
    && previous.onCopy === next.onCopy
    && previous.onQuote === next.onQuote
    && previous.onLocalDelete === next.onLocalDelete
    && previous.onRecall === next.onRecall
    && previous.canRecall === next.canRecall
    && previous.showTime === next.showTime
    && previous.animateEntry === next.animateEntry
    && previous.grouped === next.grouped
    && previousMessage.messageId === nextMessage.messageId
    && previousMessage.displayName === nextMessage.displayName
    && previousMessage.userCode === nextMessage.userCode
    && previousMessage.time === nextMessage.time
    && previousMessage.content === nextMessage.content
    && previousMessage.selfMessage === nextMessage.selfMessage
    && previousMessage.systemMessage === nextMessage.systemMessage
    && previousMessage.deliveryState === nextMessage.deliveryState
    && previousMessage.attachment === nextMessage.attachment;
}
```

（结构变化：作者行拆为 `message-who` + 气泡内右下角 `message-meta`（时间 + 刻度）；时间戳文本从作者行移入 meta；回执从文字标签换成 `deliveryTicks` 刻度并加 `aria-label`；`message-avatar` 供 peer 消息显示；`message-actions` 仍紧邻 bubble 之后（现有布局测试依赖 `bubble.nextElementSibling === actions`）；附件卡按规格 §7.4，下载钮在命令未对齐前一律禁用（§11 #4）。）

- [ ] **Step 4: 运行确认通过**

Run: `cd frontend && pnpm exec vitest run src/components/MessageItem.test.tsx src/app/WorkspacePage.test.tsx && pnpm exec tsc --noEmit`
Expected: 全绿（原有 7 个用例 + 新增 4 个）。

- [ ] **Step 5: 提交**

```bash
git add frontend/src/components/MessageItem.tsx frontend/src/components/MessageItem.test.tsx
git commit -m "feat(messages): t3 bubbles with delivery ticks and attachment cards" -- frontend/src/components/MessageItem.tsx frontend/src/components/MessageItem.test.tsx
```

---

### Task 12: useAttachmentUploads hook + MessageComposer 附件按钮与上传预览卡

**Files:**
- Create: `frontend/src/state/useAttachmentUploads.ts`
- Modify: `frontend/src/components/MessageComposer.tsx`（整文件替换）
- Test: `frontend/src/components/MessageComposer.test.tsx`（追加 7 个用例）

- [ ] **Step 1: 写失败测试（追加用例）**

`frontend/src/components/MessageComposer.test.tsx`：顶部 import 行改为：

```tsx
import { act, cleanup, fireEvent, render, screen, within } from '@testing-library/react';
```

`describe('MessageComposer', ...)` 内追加：

```tsx
  it('dispatches chooseUpload for rooms and shows the choosing card', () => {
    const bridge = new ControllableBridge();
    render(<MessageComposer state={connectedRoomState} bridge={bridge} draft="" onDraftChange={vi.fn()} onCommandResult={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '添加附件' }));

    expect(bridge.commands[0]).toMatchObject({ type: 'attachment.chooseUpload', payload: { room: 'lobby' } });
    expect(screen.getByTestId('upload-cards')).toHaveTextContent('选择中…');
  });

  it('shows determinate upload progress as chunk acks arrive', () => {
    const bridge = new ControllableBridge();
    render(<MessageComposer state={connectedRoomState} bridge={bridge} draft="" onDraftChange={vi.fn()} onCommandResult={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: '添加附件' }));
    const commandId = bridge.commands[0].id;

    act(() => bridge.publishAttachmentEvent({
      type: 'attachment.event', id: commandId,
      payload: { type: 'attachment.init', attachmentId: 'att-1234567890abcdef', uploadId: 'up-1', chunkSize: 48128, chunkIndex: 0, receivedIndexes: [], expiresAt: '2026-09-03T00:00:00Z', content: '', logicalSize: 48128 * 2 }
    }));
    act(() => bridge.publishAttachmentEvent({
      type: 'attachment.event', id: commandId,
      payload: { type: 'attachment.chunk', attachmentId: 'att-1234567890abcdef', chunkIndex: 0, content: '' }
    }));

    const card = screen.getByTestId(`attachment-card-${commandId}`);
    expect(card).toHaveTextContent('att-1234 · 上传中');
    expect(card).toHaveTextContent('1 块 / 2 块');
    expect(card).toHaveTextContent('🔒 端到端加密');
    expect(card.querySelector('.progress-fill')).toHaveStyle({ transform: 'scaleX(0.5)' });
  });

  it('marks interrupted uploads as resuming with an indeterminate bar', () => {
    const bridge = new ControllableBridge();
    render(<MessageComposer state={connectedRoomState} bridge={bridge} draft="" onDraftChange={vi.fn()} onCommandResult={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: '添加附件' }));
    const commandId = bridge.commands[0].id;

    act(() => bridge.publishAttachmentEvent({
      type: 'attachment.event', id: commandId,
      payload: { type: 'attachment.init', attachmentId: 'att-1234567890abcdef', uploadId: 'up-1', chunkSize: 48128, chunkIndex: 0, receivedIndexes: [], expiresAt: '2026-09-03T00:00:00Z', content: '' }
    }));
    act(() => bridge.publishAttachmentEvent({
      type: 'attachment.event', id: commandId,
      payload: { type: 'attachment.resume', attachmentId: 'att-1234567890abcdef', receivedIndexes: [0, 1, 2] }
    }));

    const card = screen.getByTestId(`attachment-card-${commandId}`);
    expect(card).toHaveTextContent('续传中');
    expect(card).toHaveTextContent('⚡ 连接中断，已保存 3 块');
    expect(card.querySelector('.progress-fill')).toHaveClass('progress-fill--indeterminate');
  });

  it('maps attachment errors to copy and dispatches a fresh chooseUpload on retry', () => {
    const bridge = new ControllableBridge();
    render(<MessageComposer state={connectedRoomState} bridge={bridge} draft="" onDraftChange={vi.fn()} onCommandResult={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: '添加附件' }));
    const commandId = bridge.commands[0].id;

    act(() => bridge.publishAttachmentEvent({
      type: 'attachment.event', id: commandId,
      payload: { type: 'attachment.init', attachmentId: 'att-1', uploadId: 'up-1', chunkSize: 48128, chunkIndex: 0, receivedIndexes: [], expiresAt: '2026-09-03T00:00:00Z', content: '' }
    }));
    act(() => bridge.publishAttachmentEvent({
      type: 'attachment.event', id: commandId,
      payload: { type: 'error', code: 'ErrRoomQuotaExceeded', message: 'quota' }
    }));

    const card = screen.getByTestId(`attachment-card-${commandId}`);
    expect(card).toHaveClass('upload-card--failed');
    expect(card).toHaveTextContent('房间附件配额已满（20 GiB）');

    fireEvent.click(within(card).getByRole('button', { name: '重试' }));

    expect(bridge.commands).toHaveLength(2);
    expect(bridge.commands[1]).toMatchObject({ type: 'attachment.chooseUpload', payload: { room: 'lobby' } });
    expect(screen.queryByTestId(`attachment-card-${commandId}`)).not.toBeInTheDocument();
  });

  it('removes a choosing card locally with the close button', () => {
    const bridge = new ControllableBridge();
    render(<MessageComposer state={connectedRoomState} bridge={bridge} draft="" onDraftChange={vi.fn()} onCommandResult={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '添加附件' }));
    const commandId = bridge.commands[0].id;
    expect(screen.getByTestId(`attachment-card-${commandId}`)).toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: `取消选择 ${commandId}` }));

    expect(screen.queryByTestId(`attachment-card-${commandId}`)).not.toBeInTheDocument();
  });

  it('folds completed uploads out of the card list', () => {
    const bridge = new ControllableBridge();
    render(<MessageComposer state={connectedRoomState} bridge={bridge} draft="" onDraftChange={vi.fn()} onCommandResult={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: '添加附件' }));
    const commandId = bridge.commands[0].id;

    act(() => bridge.publishAttachmentEvent({
      type: 'attachment.event', id: commandId,
      payload: { type: 'attachment.init', attachmentId: 'att-1', uploadId: 'up-1', chunkSize: 48128, chunkIndex: 0, receivedIndexes: [], expiresAt: '2026-09-03T00:00:00Z', content: '', logicalSize: 48128 }
    }));
    act(() => bridge.publishAttachmentEvent({
      type: 'attachment.event', id: commandId,
      payload: { type: 'attachment.chunk', attachmentId: 'att-1', chunkIndex: 0, content: '' }
    }));

    expect(screen.queryByTestId(`attachment-card-${commandId}`)).not.toBeInTheDocument();
    expect(screen.getByTestId('upload-cards')).toBeInTheDocument();
  });

  it('disables attachments outside room conversations', () => {
    const dmState: BridgeState = {
      ...connectedRoomState,
      navigation: { page: 'workspace', activeConversation: { kind: 'dm', id: 'B002', title: 'Bob', userCode: 'B002' } }
    };
    render(<MessageComposer state={dmState} bridge={new ControllableBridge()} draft="" onDraftChange={vi.fn()} onCommandResult={vi.fn()} />);

    expect(screen.getByRole('button', { name: '添加附件' })).toBeDisabled();
  });
```

- [ ] **Step 2: 运行确认失败**

Run: `cd frontend && pnpm exec vitest run src/components/MessageComposer.test.tsx`
Expected: FAIL —— 找不到 `添加附件` 按钮。

- [ ] **Step 3: 创建 useAttachmentUploads.ts**

创建 `frontend/src/state/useAttachmentUploads.ts`：

```ts
import { useCallback, useEffect, useState } from 'react';

import type { ChatBridgeClient } from '../bridge/types';
import {
  beginAttachmentUpload,
  dismissAttachmentUpload,
  reduceAttachmentEvent,
  type AttachmentUploadMap
} from '../bridge/attachmentEvents';

export function useAttachmentUploads(bridge: ChatBridgeClient): {
  uploads: AttachmentUploadMap;
  beginUpload(commandId: string): void;
  dismissUpload(commandId: string): void;
} {
  const [uploads, setUploads] = useState<AttachmentUploadMap>({});

  useEffect(() => bridge.subscribeAttachmentEvents((event) => {
    setUploads((current) => reduceAttachmentEvent(current, event));
  }), [bridge]);

  const beginUpload = useCallback((commandId: string) => {
    setUploads((current) => beginAttachmentUpload(current, commandId));
  }, []);

  const dismissUpload = useCallback((commandId: string) => {
    setUploads((current) => dismissAttachmentUpload(current, commandId));
  }, []);

  return { uploads, beginUpload, dismissUpload };
}
```

- [ ] **Step 4: 整文件替换 MessageComposer.tsx**

`frontend/src/components/MessageComposer.tsx` 全文替换为：

```tsx
import { useEffect, useLayoutEffect, useRef, useState } from 'react';

import { createCommand } from '../bridge/chatBridge';
import type { AttachmentUploadState, BridgeState, ChatBridgeClient, CommandResult } from '../bridge/types';
import { attachmentErrorCopy } from '../bridge/attachmentEvents';
import { useAttachmentUploads } from '../state/useAttachmentUploads';
import { CommandFeedback } from './CommandFeedback';
import { EmojiPicker } from './EmojiPicker';

export type QuoteDraft = { displayName: string; content: string };

export type MessageComposerProps = {
  state: BridgeState;
  bridge: ChatBridgeClient;
  quote?: QuoteDraft | null;
  onClearQuote?(): void;
  draft: string;
  onDraftChange(value: string): void;
  onCommandResult(result: CommandResult): void;
  focusAtEndToken?: number;
};

type Feedback = { status: 'idle' | 'pending' | 'success' | 'error'; message: string };

export function MessageComposer({ state, bridge, quote = null, onClearQuote, draft, onDraftChange, onCommandResult, focusAtEndToken = 0 }: MessageComposerProps) {
  const [feedback, setFeedback] = useState<Feedback>({ status: 'idle', message: '' });
  const [emojiOpen, setEmojiOpen] = useState(false);
  const { uploads, beginUpload, dismissUpload } = useAttachmentUploads(bridge);
  const mountedRef = useRef(true);
  const latestDraftRef = useRef(draft);
  const pendingCommandIdRef = useRef<string | null>(null);
  const pendingUnsubscribeRef = useRef<(() => void) | null>(null);
  const textareaRef = useRef<HTMLTextAreaElement>(null);
  const active = state.navigation.activeConversation;
  latestDraftRef.current = draft;

  useEffect(() => {
    mountedRef.current = true;
    return () => {
      mountedRef.current = false;
      pendingUnsubscribeRef.current?.();
      pendingUnsubscribeRef.current = null;
      pendingCommandIdRef.current = null;
    };
  }, []);

  useLayoutEffect(() => {
    if (focusAtEndToken === 0 || !textareaRef.current) return;
    const textarea = textareaRef.current;
    textarea.focus();
    textarea.setSelectionRange(textarea.value.length, textarea.value.length);
  }, [focusAtEndToken]);

  function submit() {
    const submittedDraft = draft;
    const trimmed = submittedDraft.trim();
    if (!trimmed || !active || pendingCommandIdRef.current) return;
    const submittedContent = quote ? `> ${quote.displayName}: ${quote.content}\n${trimmed}` : trimmed;
    const type = active.kind === 'room' ? 'chat.sendRoom' : 'chat.sendPrivate';
    const payload = active.kind === 'room'
      ? { room: active.id, content: submittedContent }
      : { targetUserCode: active.userCode, content: submittedContent };
    const command = createCommand(type, payload);
    pendingCommandIdRef.current = command.id;
    setFeedback({ status: 'pending', message: '正在发送…' });
    const unsubscribe = bridge.subscribeCommandResult((result) => {
      if (result.id !== command.id || pendingCommandIdRef.current !== command.id) return;
      unsubscribe();
      if (pendingUnsubscribeRef.current === unsubscribe) pendingUnsubscribeRef.current = null;
      pendingCommandIdRef.current = null;
      if (!mountedRef.current) return;
      onCommandResult(result);
      if (result.ok) {
        if (latestDraftRef.current === submittedDraft) onDraftChange('');
        if (quote) onClearQuote?.();
        setFeedback({ status: 'success', message: '消息已发送。' });
        return;
      }
      setFeedback({ status: 'error', message: result.error?.message ?? '消息发送失败。' });
    });
    pendingUnsubscribeRef.current = unsubscribe;
    bridge.dispatch(command);
  }

  function onComposerKeyDown(event: React.KeyboardEvent<HTMLTextAreaElement>) {
    if (event.key !== 'Enter' || event.shiftKey || event.nativeEvent.isComposing) return;
    event.preventDefault();
    submit();
  }

  function insertEmoji(emoji: string) {
    const textarea = textareaRef.current;
    if (!textarea) return;
    const start = textarea.selectionStart ?? draft.length;
    const end = textarea.selectionEnd ?? start;
    onDraftChange(`${draft.slice(0, start)}${emoji}${draft.slice(end)}`);
    requestAnimationFrame(() => {
      textarea.focus();
      const cursor = start + emoji.length;
      textarea.setSelectionRange(cursor, cursor);
    });
  }

  // 契约（用户定稿）：只发 chooseUpload，文件选择/加密/分块全部由 C++ 完成；
  // UI 不接触路径、明文、密钥。允许多张卡并发，一卡对应一个命令 id。
  function chooseUpload() {
    if (!active || active.kind !== 'room') return;
    const command = createCommand('attachment.chooseUpload', { room: active.id });
    beginUpload(command.id);
    bridge.dispatch(command);
  }

  function retryUpload(commandId: string) {
    if (!active || active.kind !== 'room') return;
    dismissUpload(commandId);
    chooseUpload();
  }

  return (
    <div className="composer-area">
      <CommandFeedback {...feedback} />
      <div className="upload-cards" data-testid="upload-cards">
        {Object.entries(uploads)
          .filter(([, upload]) => upload.phase !== 'completed')
          .map(([commandId, upload]) => (
            <UploadCard key={commandId} commandId={commandId} upload={upload}
              onRemove={() => dismissUpload(commandId)} onRetry={() => retryUpload(commandId)} />
          ))}
      </div>
      {quote && (
        <div className="composer-quote" data-testid="composer-quote">
          <blockquote className="composer-quote__text">&gt; {quote.displayName}: {quote.content}</blockquote>
          {onClearQuote && <button type="button" className="composer-quote__remove" aria-label="移除引用" onClick={onClearQuote}>×</button>}
        </div>
      )}
      <form className="message-composer" onSubmit={(event) => { event.preventDefault(); submit(); }}>
        <textarea ref={textareaRef} aria-label="消息输入框" rows={2} value={draft} onChange={(event) => onDraftChange(event.target.value)} onKeyDown={onComposerKeyDown} placeholder="输入消息…" />
        <button className="attach-button" type="button" aria-label="添加附件"
          title={active?.kind === 'room' ? '发送附件' : '附件暂仅支持频道会话'}
          disabled={!active || active.kind !== 'room'}
          onClick={chooseUpload}>📎</button>
        <button className="emoji-button" type="button" aria-label="表情" onClick={() => setEmojiOpen((open) => !open)}>☺</button>
        <button className="send-button" type="submit" aria-label="发送消息" disabled={!draft.trim() || pendingCommandIdRef.current !== null}>↗</button>
      </form>
      {emojiOpen && <EmojiPicker onSelect={insertEmoji} onClose={() => setEmojiOpen(false)} />}
    </div>
  );
}

function UploadCard({ commandId, upload, onRemove, onRetry }: {
  commandId: string;
  upload: AttachmentUploadState;
  onRemove(): void;
  onRetry(): void;
}) {
  const failed = upload.phase === 'failed';
  const choosing = upload.phase === 'choosing';
  const inFlight = upload.phase === 'uploading' || upload.phase === 'resuming';
  const totalChunks = upload.totalChunks;
  const progress = totalChunks && totalChunks > 0 ? Math.min(1, upload.receivedChunks / totalChunks) : undefined;
  return (
    <div className={`upload-card${failed ? ' upload-card--failed' : ''}`} data-testid={`attachment-card-${commandId}`}>
      <span className="upload-card__badge" aria-hidden="true">📎</span>
      <span className="upload-card__body">
        {failed && <>
          <span className="upload-card__title">附件发送失败</span>
          <span className="upload-card__meta upload-card__meta--error">{attachmentErrorCopy(upload.error?.code ?? '', upload.error?.message ?? '')}</span>
          <span className="upload-card__actions">
            <button type="button" onClick={onRetry}>重试</button>
            <button type="button" onClick={onRemove}>移除</button>
          </span>
        </>}
        {choosing && <>
          <span className="upload-card__title">选择中…</span>
          <span className="upload-card__meta">正在等待系统文件对话框；文件不会离开本机，之后按 47 KiB 分块加密上传</span>
        </>}
        {inFlight && <>
          <span className="upload-card__title">{upload.attachmentId ? `${upload.attachmentId.slice(0, 8)} · ` : ''}{upload.phase === 'resuming' ? '续传中' : '上传中'}</span>
          <span className={`upload-card__meta${upload.phase === 'resuming' ? ' upload-card__meta--warn' : ''}`}>
            {upload.phase === 'resuming' ? `⚡ 连接中断，已保存 ${upload.receivedChunks} 块` : `${upload.receivedChunks} 块`}{totalChunks ? ` / ${totalChunks} 块` : ''} · 🔒 端到端加密
          </span>
          <div className="progress-track" role="progressbar" aria-label={`附件上传进度 ${commandId}`}
            aria-valuemin={0} aria-valuemax={totalChunks ?? 0} aria-valuenow={upload.receivedChunks}>
            <span className={`progress-fill${progress === undefined ? ' progress-fill--indeterminate' : ''}`}
              style={progress === undefined ? undefined : { transform: `scaleX(${progress})` }} />
          </div>
        </>}
      </span>
      {!failed && <button className="upload-card__remove" type="button"
        aria-label={choosing ? `取消选择 ${commandId}` : `取消上传 ${commandId}`}
        title={choosing ? undefined : '取消命令待对齐：仅隐藏预览卡'}
        onClick={onRemove}>✕</button>}
    </div>
  );
}
```

（对比旧文件的变化：send/quote/emoji 逻辑逐字未动；新增 `useAttachmentUploads` 接线、`upload-cards` 列表（completed 自动折叠）、`attach-button`（仅频道可用，DM 禁用——payload 只有 room，契约如此）、`UploadCard` 四态渲染。标题用 attachmentId 前 8 位是契约降级（init 无 fileName，§11 #1 已请求补充）。）

- [ ] **Step 5: 运行确认通过**

Run: `cd frontend && pnpm exec vitest run src/components/MessageComposer.test.tsx && pnpm exec tsc --noEmit`
Expected: 全绿（原有 9 个用例 + 新增 7 个）。

- [ ] **Step 6: 提交**

```bash
git add frontend/src/state/useAttachmentUploads.ts frontend/src/components/MessageComposer.tsx frontend/src/components/MessageComposer.test.tsx
git commit -m "feat(composer): attachment upload cards driven by attachment.event" -- frontend/src/state/useAttachmentUploads.ts frontend/src/components/MessageComposer.tsx frontend/src/components/MessageComposer.test.tsx
```

---

### Task 13: CommandFeedback pending spinner + LAN 扫描进度条

**Files:**
- Modify: `frontend/src/components/CommandFeedback.tsx`（整文件替换）
- Modify: `frontend/src/app/App.tsx`（一处插入）
- Create: `frontend/src/components/CommandFeedback.test.tsx`

- [ ] **Step 1: 写失败测试**

创建 `frontend/src/components/CommandFeedback.test.tsx`：

```tsx
import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import { CommandFeedback } from './CommandFeedback';

describe('CommandFeedback', () => {
  afterEach(cleanup);

  it('renders nothing while idle', () => {
    const { container } = render(<CommandFeedback status="idle" message="" />);
    expect(container).toBeEmptyDOMElement();
  });

  it('pairs pending feedback with a spinner under a status role', () => {
    render(<CommandFeedback status="pending" message="正在发送…" />);

    expect(screen.getByRole('status')).toHaveTextContent('正在发送…');
    expect(document.querySelector('.command-feedback__spinner')).not.toBeNull();
  });

  it('announces errors through an alert role without a spinner', () => {
    render(<CommandFeedback status="error" message="发送失败" />);

    expect(screen.getByRole('alert')).toHaveTextContent('发送失败');
    expect(document.querySelector('.command-feedback__spinner')).toBeNull();
  });
});
```

- [ ] **Step 2: 运行确认失败**

Run: `cd frontend && pnpm exec vitest run src/components/CommandFeedback.test.tsx`
Expected: FAIL —— pending 时无 `.command-feedback__spinner`。

- [ ] **Step 3: 整文件替换 CommandFeedback.tsx**

```tsx
export type CommandFeedbackProps = {
  status: 'idle' | 'pending' | 'success' | 'error';
  message: string;
};

export function CommandFeedback({ status, message }: CommandFeedbackProps) {
  if (status === 'idle' || !message) return null;

  return (
    <p
      className={`command-feedback command-feedback--${status}`}
      role={status === 'error' ? 'alert' : 'status'}
      aria-live="polite"
    >
      {status === 'pending' && <span className="command-feedback__spinner" aria-hidden="true" />}
      <span>{message}</span>
    </p>
  );
}
```

（文本包一层 `<span>` 保持 `toHaveTextContent` 兼容；spinner 只在 pending 出现，样式来自 Task 4 tokens.css 的 `.command-feedback__spinner`。）

- [ ] **Step 4: App.tsx 插入 LAN 扫描条**

`frontend/src/app/App.tsx` 中，把：

```tsx
        {guest && (
          <section className="lan-discovery" aria-label="lan-host-discovery">
            <div className="lan-discovery__header">
```

替换为：

```tsx
        {guest && (
          <section className="lan-discovery" aria-label="lan-host-discovery">
            {discovery.scanning && <div className="lan-scan-bar" aria-hidden="true"><span className="lan-scan-bar__fill" /></div>}
            <div className="lan-discovery__header">
```

- [ ] **Step 5: 全量验证**

Run: `cd frontend && pnpm exec vitest run && pnpm exec tsc --noEmit`
Expected: 全部测试文件 passed；tsc 无错误。

- [ ] **Step 6: 提交**

```bash
git add frontend/src/components/CommandFeedback.tsx frontend/src/components/CommandFeedback.test.tsx frontend/src/app/App.tsx
git commit -m "feat(feedback): pending spinner and lan scan progress bar" -- frontend/src/components/CommandFeedback.tsx frontend/src/components/CommandFeedback.test.tsx frontend/src/app/App.tsx
```

---

### Task 14: 全量验证与收尾

**Files:** 无代码改动（验证 + 必要时清理残留）。

- [ ] **Step 1: 前端三件套**

Run: `cd frontend && pnpm exec vitest run && pnpm exec tsc --noEmit && pnpm build`
Expected: `Test Files  N passed (N)`；tsc 无输出；vite 构建成功。

- [ ] **Step 2: 打包链路（规格 §10.2）**

Run（仓库根）: `powershell -ExecutionPolicy Bypass -File scripts/build-modern.ps1 -Action Build`
Expected: 构建成功退出码 0。

- [ ] **Step 3: 浏览器验证（规格 §10.3/§10.4）**

Run: `cd frontend && pnpm dev`，用浏览器 MCP 或人工核对：
1. 布局对照两份已批准 mock（`.superpowers/brainstorm/telegram-t3-detail.html`、`telegram-t3-attachments.html`）：双栏、壁纸、气泡、成员抽屉、日期胶囊。
2. 成员抽屉动画：translateX 102% → 0，180ms。
3. 六个加载态各自成立：连接 spinner、重连计数、LAN 扫描条、历史加载、命令 pending spinner、附件卡四态。
4. **双门验证**：`document.documentElement.dataset.effects = 'off'` 后与系统 `prefers-reduced-motion: reduce` 下，全部动画冻结（CSS `!important` 兜底）。
5. 附件流：dev 用 FakeBridge，完整生命周期需真实 C++ 侧配合（`publishAttachmentEvent` 仅测试桥提供）——浏览器里验证按钮派发与 choosing 卡即可，上传中/续传/失败态用 vitest 覆盖。
6. 错误文案抽样：改 ControllableBridge/真实桥的 error code 验证映射表。
7. 回归：收发消息、引用、表情、房间成员管理、连接审批、设置页、窄窗口（<960px 侧栏收窄、☰ 出现）。

- [ ] **Step 4: 工作区检查**

Run: `git status --porcelain`
Expected: 无本计划产生的残留文件；Codex 暂存的 QML 删除保持原样（不碰）。若有本计划遗漏文件，用 pathspec 补提交；无残留则不产生空提交。

---

## 计划自检记录（writing-plans Self-Review）

**1. 规格覆盖**：§3 布局 → Task 6/7/8；§4 成员抽屉 → Task 8；§5 侧栏分段 → Task 7；§6 动效双门 + 六加载态 → Task 3/9/10/12/13；§7.2 类型 → Task 1；§7.3 错误表 → Task 2/12；§7.4 附件消息卡 → Task 11；§8 令牌 → Task 4；§9 文件清单 → 各任务 Files（global.css 改 import hub 为唯一记录在案的偏离）；§10 验证 → Task 13 Step 5 / Task 14；§11 待对齐 → 全部以可选字段 + 降级 UI + 禁用态承接（#1 logicalSize、#2 取消/重试命令、#4 接收下载），无自造协议。

**2. 占位符扫描**：无 TBD/TODO/「同 Task N」；所有代码步骤均给全文或带上下文的精确 diff。

**3. 类型一致性**：`WorkspaceSection` 由 ConversationSidebar 导出（Task 7 起），WorkspacePage 从该文件 import；`AttachmentUploadMap/State`、`reduceAttachmentEvent`、`attachmentErrorCopy` 签名在 Task 2/12 一致；`MessageItemProps.grouped` 在 Task 10 Step 4 先补类型、Task 11 完整接线；`MessageAttachment` 字段与 Task 11 渲染一致；`subscribeAttachmentEvents` 在 Task 1 定义、Task 12 hook 消费。

**执行顺序约束**：Task 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11 → 12 → 13 → 14 必须顺序执行（CSS 层先于 JSX 重排，避免中间态样式坍塌；Task 10/11 有类型依赖）。
