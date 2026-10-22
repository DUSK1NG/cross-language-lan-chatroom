# LAN Chat 前端 UI 设计规范（Telegram Desktop 深色风格）

适用于 `frontend/`。技术栈：React 19 + TypeScript、Tailwind CSS v4、shadcn/ui 风格组件（Radix UI）、Framer Motion、lucide-react、@tanstack/react-virtual、emoji-mart。未使用任何 Telegram 素材。

## 1. 设计令牌

唯一来源：`frontend/src/styles/tokens.css`。`app.css` 的 `@theme inline` 只把令牌映射成 Tailwind 工具类（`bg-bubble-out`、`text-ink-2`、`rounded-bubble` …），不定义新颜色。ESLint 的 `no-restricted-syntax` 禁止在 `src/` 中写十六进制或 `rgb()` 颜色字面量（测试文件除外）。

### 1.1 颜色（深色默认 / 浅色 `html[data-theme='light']`）

| 令牌 | 深色 | 浅色 | 用途 |
|---|---|---|---|
| `--bg` | #0E1621 | #FFFFFF | 聊天区底色（叠加 ≤ 4% 点阵） |
| `--bg-sidebar` | #17212B | #FFFFFF | 会话列表、头部、面板 |
| `--bg-hover` | #202B36 | #F1F3F5 | 悬停 |
| `--bg-active` | #2B5278 | #3D72AE | 选中会话 |
| `--bubble-out` / `--bubble-in` | #2B5278 / #182533 | #E1F0FF / #FFFFFF | 自己 / 对方气泡 |
| `--text` / `--text-2` / `--text-3` | #F5F5F5 / #8B9BAB / **#7D8B99** | #0F1419 / #5F6B76 / #66727D | 正文 / 次要 / 辅助 |
| `--accent` | #5288C1 | #3D72AE | 下划线、焦点环、进度 |
| `--online` / `--danger` / `--warning` | #4FAE4E / #E53935 / #F5A623 | 同左（danger 文字 #C62828） | 状态 |
| `--divider` | rgba(255,255,255,.06) | rgba(0,0,0,.08) | 分隔线 |

强调色预设（设置 › 外观）：`html[data-accent=blue|green|violet|orange|rose]`，深色与浅色各一套，替换 accent / accent-strong / bg-active / bubble-out 等。

### 1.2 尺寸与动效

- 圆角：6 / 10 / 14（气泡）/ 16（弹窗、卡片）/ 9999；气泡尾角 4px，只在一组消息的最后一条。设置可选小 8 / 标准 14 / 大 18。
- 间距：Tailwind 的 `--spacing` 固定为 4px，只用 4 / 8 / 12 / 16 / 24 / 32；2px 只用于同一组消息之间，组与组之间 12px。
- 字号：正文 14、辅助 12、标题 16、名称 15；消息字号可选 13 / 14 / 16。字重 400 / 500，600 只用于标题和名称。
- 动效：悬停 150ms、进入 200ms、面板 300ms，统一 `cubic-bezier(.2,.8,.2,1)`。时长乘以原生性能档位给出的 `--motion-scale`。`html[data-effects='off']` 下所有过渡和动画直接到达终态（同时设置 Framer `MotionConfig reducedMotion="always"`）。`prefers-reduced-motion` 下只保留淡入淡出。

### 1.3 对比度修正（WCAG AA：文字 ≥ 4.5:1，图标 ≥ 3:1）

原规格中以下颜色组合达不到 AA，已新增或调整令牌。所有数值均为计算结果：

| 场景 | 原值 | 对比度 | 新令牌 | 对比度 |
|---|---|---|---|---|
| 辅助文字在 `--bg` 上 | #6C7883 | 4.03 | `--text-3` #7D8B99 | 5.21（在侧栏上 4.67） |
| 自己气泡里的时间 | #8B9BAB | 2.86 | `--bubble-out-meta` rgba(255,255,255,.70) | 4.94 |
| 自己气泡里的已读勾 | #5288C1 | 2.19 | `--read-tick-out` #8CC8FF | 4.58 |
| 白字角标 / 主按钮 | #5288C1 | 3.72 | `--accent-strong` #3D72AE | 4.98 |
| 链接文字在侧栏上 | #5288C1 | 4.38 | `--accent-text` #6399D0 | 5.43 |
| 选中行的第二行文字 | #8B9BAB | 2.86 | `--text-on-active-2` rgba(255,255,255,.72) | 5.12 |
| 白字危险按钮 | #E53935 | 4.23 | `--danger-strong` #D32F2F | 4.98 |
| 危险文字在侧栏上 | #E53935 | 3.85 | `--danger-text` #FF6B66 | 5.86 |
| 自己气泡里的引用（深色） | 70% 白，叠 10% 白底 | 3.74–4.44 | `--quote-out-text` 85% 白 | 4.84–5.93 |
| 对方气泡里的引用（深色） | 叠 accent-soft 底 | 4.19 | `--quote-in-bg` 4% 白 | 4.62 |
| 浅色主题自己气泡里的引用作者 | 白色 | < 1.2 | `--quote-out-name` = accent-text，叠 50% 白底 | 5.07–6.54 |

头像渐变两端与白色首字母的对比度 ≥ 4.6；8 种发送者名称色在对方气泡上 ≥ 6.9（浅色主题 ≥ 4.8）。

## 2. 布局

- 三栏：会话列表 320px（可拖动 260–420px，宽度存于 `localStorage['lan-chat.sidebarWidth']`）、聊天区、信息面板 340px（默认收起）。
- 窗口宽度 < 900px：信息面板改为浮层，背后加遮罩。< 700px：列表和聊天交替显示，聊天头部有返回按钮。
- 最小窗口 480×600（Qt `setMinimumSize`），任何宽度都不出现横向滚动条。
- 不做自定义标题栏：Qt 无边框窗口需要重做贴靠、阴影、DPI 和多显示器支持，`-webkit-app-region` 只在 Electron 中可用。

## 3. 组件

| 区域 | 文件 | 要点 |
|---|---|---|
| 基础组件 | `components/ui/*` | Button、IconButton（必须有 aria-label 和 tooltip）、Avatar、Dialog（16px 圆角，缩放 .96→1）、DropdownMenu / ContextMenu、Switch / SegmentedRadio / Select、Skeleton / EmptyState / ErrorState、Toaster（右下角，3s） |
| 会话列表 | `components/sidebar/*` | 汉堡菜单、胶囊搜索框（≥ 6 个会话时显示）、全部 / 群聊 / 私聊标签带滑动下划线（默认 群聊）、64px 行、48px 头像、99+ 角标、在线点、虚拟滚动、空状态提供“创建或加入频道” |
| 聊天头部 | `components/chat/ChatHeader.tsx`、`ConnectionBanner.tsx` | 56px，副标题“N 名成员，M 人在线”；搜索、审批角标（数量增加时弹跳）、更多菜单；重连横幅 |
| 消息列表 | `components/chat/MessageList.tsx` | 底部锚定、虚拟化、回到底部按钮带新消息数、吸顶日期胶囊、“以下为新消息”分隔线；点击引用跳到原消息并高亮 1s |
| 气泡 | `components/chat/MessageBubble.tsx`、`RichText.tsx`、`FileCard.tsx` | 最大宽度 65%；5 分钟内同一作者归为一组；时间和状态跟在最后一行文字后；链接、代码、路径；悬停操作条（回复 / 复制 / 更多）与右键菜单；不渲染空气泡 |
| 输入框 | `components/composer/*` | 自动增高 1–6 行；Enter 发送、Shift+Enter 换行；输入法上屏的回车不发送；Esc 取消回复；草稿按会话保存；emoji-mart 懒加载；有文字时才出现发送按钮 |
| 面板与弹窗 | `components/shell/*`、`components/dialogs/*` | 信息面板、宽度拖动条、审批弹窗（60 秒倒计时环）、新建 / 管理频道（删除需要二次确认）、成员资料、管理操作确认 |
| 页面 | `app/*` | 工作区、设置（账户 / 外观 / 通知 / 隐私与安全 / 关于）、模式选择与连接页、错误页 |

审批倒计时以服务端的 `requestedAt`（Go `CreatedAt`，超时时间 60s）为起点；只有当该时间在 0–60s 之前时才采用，否则从界面第一次看到请求时开始计时。超时后按钮禁用，条目变灰，1.5s 后移除。

## 4. 快捷键

| 键 | 作用 |
|---|---|
| Ctrl+K | 聚焦会话搜索 |
| Ctrl+F | 搜索当前会话的消息 |
| Ctrl+↑ / Ctrl+↓ | 切换到上一个 / 下一个会话 |
| Esc | 关闭表情面板；在输入框内取消回复；否则依次关闭消息搜索、信息面板（弹窗自行处理 Esc，审批中的弹窗不可关闭） |

## 5. 受协议限制、未实现或降级的功能

不改桥接协议、状态结构和 Go 服务端，因此以下功能只能降级或暂不实现：

- 已读回执：协议只有 queued / sent / delivered，界面显示 1 勾 = 已发送，2 勾 = 已送达，不显示“已读”。
- 正在输入、表情回应、Alt+↑ 编辑上一条、最后上线时间、会话置顶 / 静音、列表里的最后一条消息预览：协议没有相应字段或命令。
- 日期：消息时间只有 “HH:mm”，因此只显示“今天”胶囊，没有跨日分隔。
- 私聊的在线状态只能从 `state.members` 推断。
- 引用跳转按“作者 + 原文”匹配，协议中没有被引用消息的 ID。
- 拖入或粘贴文件只会打开原生文件选择窗口：按附件安全约定，页面不能读取本地文件。
- 图片缩略图（依赖 spec 03）、取消上传（没有对应命令）均未实现。
- 桥接一次最多下发 500 条消息（`kMaxSerializedRows`）。
- 设置中的“通知”显示为“暂未开放”。
