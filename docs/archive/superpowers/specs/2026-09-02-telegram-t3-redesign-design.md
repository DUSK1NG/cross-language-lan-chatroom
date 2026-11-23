# Telegram Air（T3）前端重设计 — 设计文档

- 日期：2026-09-02
- 状态：视觉稿已确认；规格按 2026-09-02 Codex 提供的附件命令/回执契约修订（§7.2）
- 视觉稿（本地文件，`.superpowers/` 不入库，双击浏览器打开）：
  - `telegram-t3-detail.html` — 整体布局、会话列表、气泡、成员抽屉、加载态演示
  - `telegram-t3-attachments.html` — 附件卡、上传进度、加密标识、错误映射
- 范围：**仅 `frontend/`**（React + TS + Vite，运行于 Qt WebEngine / QWebChannel）。不改动 Codex 正在进行的 C++（client-cpp/gui）与 Go（server-go）附件实现。

## 1. 背景与目标

当前 UI 由一份 829 行的 `frontend/src/styles/global.css` 加两层已死/互相覆盖的旧样式层组成，视觉方向为雾蓝。用户决定整体换向 Telegram 桌面端的轻快风格（明确否决 Discord Slate 方向），同时要求：

1. 动画与加载态贴合现有 bridge 信号，纯 CSS 实现，双门控（性能开关 + 系统减少动效）。
2. UI 贴合 Codex 正在开发的端到端加密与文件传输功能——如实渲染其状态机与错误面，不为 UI 发明不存在的状态。

成功标准：`tsc` 0 错误、`vitest` 全绿、CMake 构建通过、浏览器中对照视觉稿逐项核对通过；旧行为（收发消息、管理操作、审批、设置）无回归。

## 2. 决策记录

| 决策 | 结论 | 备注 |
|---|---|---|
| 风格方向 | T（Telegram Air），否决 H（Discord Slate）与雾蓝延续 | 用户选定 |
| 结构方案 | T3：双栏 + 成员面板改为右侧覆盖抽屉 | 备选 T1 三栏、T2 抽屉全屏被否 |
| 强调色 | `#2F7BE5` | 刻意不用 Telegram 官方 `#3390EC`，也不用 blurple |
| 外发气泡 | 保留绿色 `#E3FEE0` | 默认值，确认时未否决 |
| 动效时长 | spinner 0.8s linear infinite；常规过渡 180ms；抽屉 180ms ease-out | 默认值，确认时未否决 |
| 抽屉背板 | 不加遮罩背板（Telegram 式纯滑出） | 默认值，确认时未否决 |
| 旧样式层 | 三层旧 CSS 全部移除，替换为单一 `--t-*` 令牌层 | 非增量改造 |

## 3. 设计令牌（`--t-*` 层）

写入 `frontend/src/styles/global.css`，作为唯一令牌来源；现存的雾蓝变量与两层旧覆盖层删除。

```css
:root{
  --t-accent:        #2F7BE5;   /* 主操作、链接、✓✓、锁 chip */
  --t-accent-soft:   #E7F0FC;   /* 强调底色、文件类型贴片 */
  --t-bubble-out:    #E3FEE0;   /* 外发气泡 */
  --t-bubble-in:     #FFFFFF;   /* 内发气泡 */
  --t-wall:          #EDEEF0;   /* 聊天壁纸基色 */
  --t-ink:           #1D1F22;   /* 主文本 */
  --t-ink-2:         #70767E;   /* 次级文本 */
  --t-ink-3:         #9AA1A9;   /* 弱化文本、占位符 */
  --t-online:        #2EA760;   /* 在线、成功 */
  --t-warn:          #B87926;   /* 断线续传、警示 */
  --t-danger:        #C93B3B;   /* 失败、危险操作 */
  --t-line:          #E4E6EA;   /* 分隔线、描边 */
  --t-chip:          #F1F2F4;   /* 胶囊、悬停底色 */
  --t-bubble-radius: 12px;      /* 气泡圆角，尾角 3px */
  --t-bubble-shadow: 0 1px 1px rgba(0,0,0,.10);
}
```

聊天壁纸：`--t-wall` 基色 + 两层 `radial-gradient` 圆点（46px 平铺，两层圆点相位错开），见视觉稿实现。

## 4. 工作区布局（T3）

现状 `frontend/src/app/WorkspacePage.tsx:208-217`：四栏 `60px 232px minmax(0,1fr) 224px`（WorkspaceRail / ConversationSidebar / chat-region / MemberPanel）。

目标：

- 网格改为 `292px minmax(0,1fr)` 两栏。
- **WorkspaceRail 删除**（24 行组件）：品牌猫图 + 群/私分段 + ⚙ 合并进 ConversationSidebar 顶部；现有 `sidebarOpen` ModalSurface（`WorkspacePage.tsx:218`）作为窄窗口回退路径保留，其触发按钮随之并入侧栏。
- **MemberPanel 变右侧覆盖抽屉**：`position:absolute; top:0; right:0; height:100%; width:280px; transform:translateX(102%);` 打开时 `translateX(0)`，`transition:transform 180ms ease-out`。JSX 内容不变，仅重新定位。无背板遮罩。
- **删除重复渲染**：`WorkspacePage.tsx:219` 的成员 ModalSurface 与内联 MemberPanel 是两份实例；抽屉化后只保留抽屉一份，`memberDrawerOpen` 驱动抽屉而非弹窗。
- 管理操作（连接审批、频道管理）留在 ChatHeader——阻塞式审批弹窗不降级为抽屉。

## 5. 组件规格

### 5.1 ConversationSidebar

- 顶部：猫图（品牌）+ `群聊/私信` 分段控件（沿用现有 `section` 状态）+ ⚙ 设置按钮。
- 下方：胶囊形搜索框（`--t-chip` 底、全圆角）。
- 列表行：头像、名称、成员数/未读徽标（沿用现有数据：`RoomSummary.memberCount/unreadCount`）；MLS 会话在名称后加 🔒 小徽标（见 7.4）。
- `directory.refreshRooms/refreshUsers` 现有 fire-and-forget 刷新逻辑不动。

### 5.2 ChatHeader

- 左：头像 + 会话名；副标题行 = 原有 `statusText`（`:22`）+ MLS 会话追加 `🔒 端到端加密` chip（`--t-accent-soft` 底、`--t-accent` 字，圆角胶囊）。
- 右：搜索消息、频道管理（`canManage`）、连接审批（`identity.admin`，含待审批数）、成员（开合抽屉）。现有 props 不变。

### 5.3 MessageTimeline / MessageItem

- 气泡：入发 `--t-bubble-in` 左尾角 3px，外发 `--t-bubble-out` 右尾角 3px，其余角 12px；投影 `--t-bubble-shadow`。
- 同发送者分组：相邻同 `userCode` 的消息隐藏头像、收紧间距、仅组首有尾角（沿用 `MessageItem` 现有 selfMessage/systemMessage 逻辑，新增"与上一条同源"判定）。
- 日期胶囊（"今天"）与系统消息胶囊：居中，`rgba(255,255,255,.85)` 底。系统消息沿用 `MessageItem.systemMessage`。
- ✓✓ 回执：`deliveryState` 为 `delivered` 时双勾染 `--t-accent`；`sent` 单勾 `--t-ink-3`；`failed` 红色 `!`（现有字段，`:38`）。
- 空态与「↓ N 条新消息」跳底按钮保留现有文案与行为（`bottomThresholdPx=32`）。
- 引用回复样式：左侧 3px `--t-accent` 竖条 + 灰底引文（现有 quote 逻辑）。

### 5.4 MessageComposer

- 输入区：`--t-chip` 圆角容器、`--t-accent` 圆形发送按钮、➕ 附件按钮（触发选件，见 7.2）。
- 上传预览卡（选中文件后出现在输入区上方）。UI 状态由 `commandResult` 事件流归一、按命令 id 关联（§7.2）：

| UI 状态 | 触发 | 文案 / 行为 |
|---|---|---|
| 选择中 | `chooseUpload` 已发、init 未归 | 「选择中…」（等待文件选择器与建传） |
| 上传中 | `attachment.init`（`receivedIndexes` 空）后逐块 `attachment.chunk` | 「上传中 {received}/{total} 块 · {MiB}」+ 进度条 + 取消 ✕ |
| 续传中 | init 带非空 `receivedIndexes`，或 `attachment.resume` 回执 | 「⚡ 连接中断，已保存 {n} 块，正在续传…」→「续传中 …」（`--t-warn`） |
| 已完成 | 已确认块数 == 总块数 | 「已完成 · 端到端加密」（`--t-online`），卡片随即折叠 |
| 失败 | `error` 回执 | 红字错误文案（按 7.3 映射）+ 重试 / 移除 |
| 取消 | 用户点 ✕ | 卡片移除（取消命令形态待对齐，§11） |

- 卡片常显信息：文件名、大小（MiB）、`{total} 块（47 KiB/块）`、🔒 端到端加密。**依赖 init 载荷补充 `fileName`/`logicalSize`（§11）**；未补充前降级为：attachmentId 前 8 位 + 不定态进度条（`scaleX` 往复）+ 已收块数。
- 客户端预检（500 MiB）由 C++ 在读文件时执行；React 不接触文件，超限以 `error` 回执到达，展示对应文案。

### 5.5 MemberPanel（抽屉内容）

- 分组：房主 / 在线 / 离线；行内头像 + 在Presence 圆点（`--t-online`）+ 管理员徽标 + 私聊/管理按钮——JSX 全部沿用，仅容器变抽屉。

### 5.6 CommandFeedback

- `pending` 态追加 12px 旋转环（6.2 的 spinner）；`role/aria-live` 语义不变。

## 6. 动画与加载态

### 6.1 六个加载/过渡指示（全部对应真实 bridge 信号）

| # | 信号 | 表现 |
|---|---|---|
| 1 | `connection.phase ∈ {connecting, reconnecting}` | ChatHeader 副标题处 16px 旋转环 + `statusText`；`reconnectAttempt` 显示「第 n 次重连」 |
| 2 | 历史分页（`MessageTimeline.tsx:59-71` 触顶加载，现为**无提示静默加载**） | 时间线顶部插入旋转环 + 「加载历史消息…」；实现走组件本地状态：触发分页时置位、`activeMessages` 变长后撤除，无需新增 bridge 事件 |
| 3 | `deliveryState === 'queued'` | 消息气泡时间旁灰色时钟图标（CSS 动画脉动） |
| 4 | 命令执行 pending | CommandFeedback 旋转环（5.6） |
| 5 | `lanDiscovery.scanning` | 主机列表顶部细进度条（`scaleX` 往复） |
| 6 | 附件传输 | 5.4 / 7 的进度条与块数文案 |

### 6.2 门控规则（双重门）

- `<html data-effects="on|off">` 由 `state.performance.effectsEnabled` 驱动（App 层写入；**这是对现状死门的修复**——`types.ts:81` 已声明 `effectsEnabled` 但当前零组件读取）。
- `@media (prefers-reduced-motion: reduce)` 并行兜底。
- 关闭效果时：无限循环动画显示**静止帧**（冻结的环/条），而非隐藏；一次性过渡直接跳终态。
- **无限循环例外**：加载 spinner 的 `0.8s linear infinite` 是既有「过渡 ≤160ms」规则声明的显式例外，除此之外所有动画仍只用 `transform`/`opacity`。
- gsap 三件套（`animation/motion.ts` 的 0.16/0.18/0.18s 一次性入场）保留，但 `prefersReducedMotion()` 之外再叠加 `effectsEnabled` 判断（motion.ts 增加第二个门参数）。

### 6.3 进度条实现约束

所有进度条（附件上传/下载、LAN 扫描）用内层元素 `transform: scaleX(p)` 驱动，`transform-origin:left`——不用宽度、不用 conic-gradient，满足 transform/opacity-only 约束。

## 7. 附件与端到端加密

### 7.1 现状契约（Codex 已实现/在建，UI 只消费不发明）

- 客户端状态机 `AttachmentTransferState`：`Idle|Initializing|Uploading|Resuming|Completed|Cancelled|Failed`，`MaxChunkRetries=3`，`ChunkSize=47 KiB`。
- 命令/回执契约 2026-09-02 由 Codex 确认（§7.2）：React 发 `attachment.chooseUpload`，经 `commandResult` 收 `attachment.event`。
- 每块 AEAD（key 32B / nonce 12B / tag 16B），nonce 由 `ChunkContext{attachment_id, room, group_id, logical_size, chunk_size, chunk_index}` 派生；每块带密文 sha256。
- 协议：`attachment.init`（服务器返回 `attachment_id/upload_id/chunk_size/expires_at`，24h）+ `attachment.chunk`。
- 限额：500 MiB/文件、20 GiB/房间配额（原子预留）、24h 过期。
- 服务器错误哨兵 10 个（`server-go/attachment_store.go:31-40`）。

### 7.2 前端桥接契约（Codex 定稿，2026-09-02）

**命令**（React → C++，经 bridge dispatch）。附件按钮唯一动作：

```json
{ "type": "attachment.chooseUpload", "id": "<命令id>", "payload": { "room": "<房间>" } }
```

- React 生成命令 id、发送命令、记录该 id 用于关联回执，然后进入「选择中」。
- C++ 弹出 Windows 文件选择器，本地生成独立 AES-256 密钥，读取文件并按 47 KiB（48128 字节）分块做 AES-256-GCM 加密、自动上传。**React 不接触本地路径、明文内容、密钥或 nonce，不自行读文件、不实现加密。**

**回执**（C++ → React，出现在 `commandResult`，外层 `type: "attachment.event"`，以 `id` 关联发起命令）：

| `payload.type` | 含义 | 关键字段 |
|---|---|---|
| `attachment.init` | 建传确认（含续传基线） | `attachmentId` `uploadId` `chunkSize`(48128) `chunkIndex` `receivedIndexes[]` `expiresAt` `content:""` |
| `attachment.chunk` | 分块回执 | 按命令 id 累计已确认块数 |
| `attachment.resume` | 续传回执 | 中断后续传 |
| `error` | 失败 | 错误码 → §7.3 映射 |

TypeScript（新增于 `frontend/src/bridge/types.ts`，字段名照抄载荷）：

```ts
export type AttachmentEventPayload =
  | { type: 'attachment.init'; attachmentId: string; uploadId: string;
      chunkSize: number; chunkIndex: number; receivedIndexes: number[];
      expiresAt: string; content: string }
  | { type: 'attachment.chunk'; attachmentId: string; chunkIndex: number;
      content: string }
  | { type: 'attachment.resume'; attachmentId: string;
      receivedIndexes: number[] }
  | { type: 'error'; code: string; message: string };

export type AttachmentEvent = {
  type: 'attachment.event';
  id: string;                    // 关联发起的 chooseUpload 命令 id
  payload: AttachmentEventPayload;
};
```

（`attachment.chunk`/`attachment.resume` 的确切字段以 Codex 实现为准；UI 不依赖上表未列出的字段。）

- 一次 `chooseUpload` 命令 = 一次上传；预览卡按命令 id 建立，`completed`/`cancelled`/`failed` 终态后折叠或移除，支持并发多卡。
- UI 内部把事件流归一为 `选择中|上传中|续传中|已完成|失败` 渲染态（§5.4）；不再假设 bridge 直接推送状态机枚举。
- 取消与失败重试的命令形态待定（§11）；接收侧 `MessageItem.attachment` 元数据来源同待对齐。

### 7.3 服务器错误 → UI 文案（10 条全覆盖）

错误以 `error` 回执经 `commandResult` 到达；`code` 字符串与哨兵的对应待对齐（§11），下表按哨兵列出。

| 哨兵 | 文案 | 处理 |
|---|---|---|
| `ErrAttachmentTooLarge` | 文件超过 500 MiB 上限 | C++ 读文件时拦截，React 展示；卡片标红 + 移除 |
| `ErrRoomQuotaExceeded` | 房间附件配额已满（20 GiB） | 卡片标红 |
| `ErrInvalidAttachmentSize` | 文件大小无效 | 卡片标红 + 移除 |
| `ErrAttachmentUploadNotFound` | 传输异常，请重试 | 自动重试 ≤3 次 |
| `ErrAttachmentUploadUnauthorized` | 没有在此频道发送附件的权限 | 标红，不可重试 |
| `ErrAttachmentUploadExpired` | 上传会话已过期（24 小时），请重新发送 | 重试按钮重走 init |
| `ErrAttachmentChunkOutOfRange` | 传输异常，请重试 | 自动重试 |
| `ErrAttachmentChunkTooLarge` | 传输异常，请重试 | 自动重试 |
| `ErrAttachmentChunkHashMismatch` | 分块校验失败，正在自动重传 (n/3)… | 重传该块；接收侧同文案 |
| `ErrAttachmentChunkConflict` | 传输异常，请重试 | 自动重试 |

三次自动重试失败 → `failed` 态 → 手动重试按钮。接收侧四态：待下载（圆形 ↓ 按钮）、下载中（进度条 + 块数）、校验失败（红 + ↻ 重试）、已过期（灰字「文件已过期（超过 24 小时），请让发送者重新发送」，按钮禁用）。

### 7.4 附件消息卡（MessageItem 内）

- 结构：类型贴片（PDF/DOC/ZIP 等，按扩展名映射底色）+ 文件名（溢出省略）+ 大小 + 🔒 端到端加密 + 右侧操作按钮。
- 发出完成态：卡下 meta 行 ✓✓（`--t-accent`）。
- 不做图片/视频缩略图（本阶段协议无缩略图管道），一律文件卡。
- 上传与消息发送的编排（上传完成后消息才进时间线，或消息先行带附件引用）待对齐（§11）；确认前按「完成后出消息」设计。

### 7.5 加密标识

- **会话头**：副标题 `🔒 端到端加密` chip（仅 MLS 会话）。
- **会话列表**：MLS 会话名后 🔒 小徽标。
- **不做**：每条文本消息的挂锁（噪声 > 信息）。
- **不动**：房主证书指纹核对流程（`frontend/src/app/App.tsx:212`）保持现状。
- 假设：仅 MLS 会话允许附件与显示锁；普通会话是否可传待对齐（§11）。

## 8. 非目标

- 图片/视频缩略图与预览管道
- 独立的传输管理面板/全局下载列表
- 深色主题（另立规格）
- C++ 控制器、Go 服务器、协议层的任何改动
- 不修复与本次无关的旧债（如 CMake 中死掉的 Qt6::Quick 链接——属 Codex 领域，另行处理）

## 9. 改动面（文件清单）

| 文件 | 改动 |
|---|---|
| `frontend/src/styles/global.css` | 重写为 `--t-*` 令牌层；删除旧三层样式；壁纸、气泡、卡片、抽屉、全部 keyframes |
| `frontend/src/bridge/types.ts` | 新增 7.2 的类型与 MessageItem.attachment |
| `frontend/src/app/WorkspacePage.tsx` | 两栏网格、移除 WorkspaceRail 挂载、成员抽屉替代内联面板 + 弹窗重复 |
| `frontend/src/components/WorkspaceRail.tsx` | 删除（含 `WorkspaceRail.test.tsx`；逻辑并入 ConversationSidebar） |
| `frontend/src/components/ConversationSidebar.tsx` | 顶部分段控件 + 搜索胶囊 + 锁徽标 |
| `frontend/src/components/ChatHeader.tsx` | 锁 chip、抽屉开合改接抽屉 |
| `frontend/src/components/MessageTimeline.tsx` | 气泡/分组/胶囊样式挂钩、历史分页加载指示 |
| `frontend/src/components/MessageItem.tsx` | 气泡渲染、✓✓、附件消息卡（五态） |
| `frontend/src/components/MessageComposer.tsx` | 附件按钮、上传预览卡（状态机映射） |
| `frontend/src/components/MemberPanel.tsx` | 仅容器类名/定位，JSX 不动 |
| `frontend/src/components/CommandFeedback.tsx` | pending 旋转环 |
| `frontend/src/animation/motion.ts` | 增加 effectsEnabled 第二道门 |
| `frontend/src/app/App.tsx` | 写入 `<html data-effects>` |

对应测试文件同步更新（`*.test.tsx` 13 个，现有 81 用例不回退）。

## 10. 验证计划

1. `npx tsc --noEmit` 0 错误；`npx vitest run` 全绿。
2. `scripts/build-modern.ps1 -Action Build` 通过。
3. 浏览器（Qt WebEngine 或 dev server）对照两份视觉稿逐项核对：布局、气泡、抽屉动画、六个加载态（含开关与系统减少动效两种降级）、上传预览全流程、错误文案抽样。
4. 回归：收发消息、引用、表情、房间/成员管理、连接审批、设置页、窄窗口侧栏回退。

## 11. 需与 Codex 对齐的问题（不阻塞 UI 开发，阻塞联调）

1. **init 载荷补充 `fileName` 与 `logicalSize`**——C++ 侧 `begin(logicalSize, chunkSize)` 已持有；缺失时 UI 无文件名/大小/总块数，进度只能是不定态条（已提出请求）。
2. 取消（预览卡 ✕）与失败重试的命令形态：`attachment.cancelUpload {attachmentId}`？重试是重发 `chooseUpload` 还是恢复原上传？
3. 用户在文件选择器点「取消」时是否回执（`error` 还是静默——静默则 UI 需要超时兜底收卡）。
4. 接收侧（下载）状态机与消息内附件元数据（`MessageItem.attachment`）来源；上传与消息发送的编排（完成后出消息 or 消息先行）。
5. `error.code` 的字符串集合与服务器哨兵的对应关系。
6. 非 MLS 会话是否允许发附件（决定锁 chip 语义）。
7. `expiresAt` 的确切格式（ISO 8601 假设）。
