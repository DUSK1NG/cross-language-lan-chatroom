# LAN Chat 前端 UI 设计规范

适用于 `frontend/`。技术栈：React 19 + TypeScript、Tailwind CSS v4、shadcn/ui 风格组件（Radix UI）、Framer Motion、lucide-react、@tanstack/react-virtual、emoji-mart。聊天区的半透明材质和动效参考 [Vibe Design Tips 的 Cirrus](https://vibedesign.tips/012-message-bubbles/)。

## 1. 设计令牌

唯一来源：`frontend/src/styles/tokens.css`。`app.css` 的 `@theme inline` 只把令牌映射成 Tailwind 工具类（`bg-bubble-out`、`text-ink-2`、`rounded-bubble` …），不定义新颜色。ESLint 的 `no-restricted-syntax` 禁止在 `src/` 中写十六进制或 `rgb()` 颜色字面量（测试文件除外）。

### 1.1 颜色（Claude 色卡；深色默认 / 浅色 `html[data-theme='light']`）

2026-11 迁移：令牌取自 [dsh-claude-style](https://github.com/Nwflower/dsh-claude-style)（MIT）的 Claude 色卡，桌面端与 Android 端同源。强调色默认 terracotta（`data-accent='orange'`），不再默认蓝色。

| 令牌 | 深色 | 浅色 | 用途 |
|---|---|---|---|
| `--bg` | #191919 | #FFFFFF | 聊天区画布 |
| `--bg-sidebar` | #1F1F1F | #FAFAFA | 会话列表、头部、面板 |
| `--bg-hover` | #242424 | #F2F0EC | 悬停 |
| `--bg-active` | #2B2B2B | #E9E6DF | 选中会话 |
| `--bubble-out` / `--bubble-in` | #262626 / #1F1F1F | #F0EEE9 / #FFFFFF | 自己 / 对方气泡（均为中性面，不用品牌色铺底） |
| `--text` / `--text-2` / `--text-3` | #FDFDFD / #B8B5AD / #8F8C85 | #161616 / #3D3D3A / #6B6B66 | 正文 / 次要 / 辅助 |
| `--accent` | #D97757 | #B4562F | 下划线、焦点环、进度、徽标底 |
| `--accent-strong` | #B34D2A | #9C4527 | 白字主按钮 / 角标 |
| `--accent-text` | #E08A6D | #9C4527 | 强调文字（时间、链接、分组标题） |
| `--online` / `--danger` / `--warning` | #6C9E6C / #C4453A / #D9A441 | 同左 | 状态 |
| `--divider` | rgba(255,255,255,.09) | rgba(0,0,0,.10) | 0.5px 分隔线 |
| `--border-control` | rgba(255,255,255,.14) | rgba(0,0,0,.16) | 输入框 / 卡片描边 |

强调色预设（设置 › 外观）：`html[data-accent=blue|green|violet|orange|rose]`，深色与浅色各一套，只换强调色；中性面与气泡始终走 Claude 灰阶。默认值为 `orange`（terracotta）。

### 1.2 尺寸与动效

- 圆角令牌包括基础组件、气泡、卡片与胶囊。相邻气泡在发送者一侧使用较紧的圆角，设置可选小 8 / 标准 14 / 大 18。
- Tailwind 的 `--spacing` 为 4px。聊天区组内消息行连续（间距 0），组间 16px（`MessageList.tsx` 首行 `pt-4`）。
- 几何：顶部 header 52px；侧栏默认 260px（可拖 240–380）；会话行 56px，行内头像 32px；身份卡 52px、头像 28px；composer 最大宽 768px、圆角 16px、无投影。
- 字号：正文 14、辅助 12、标题 16、名称 15；消息字号可选 13 / 14 / 16。字重 400 / 500，600 只用于标题和名称。
- 气泡入场淡入并逐步显出磨砂，退出时逐渐淡出。应用的省电档位使过渡和动画直接到达终态。
- 动效令牌（`frontend/src/styles/tokens.css`）：press 80 / hover 120 / exit 120 / panel 250 / enter 320 / send 400 ms；缓动 `--ease: cubic-bezier(.22, 1, .36, 1)`，支持的浏览器换成 `linear()` 弹簧；`--motion-scale` 统一缩放全部时长。
- 省电档 `html[data-effects='off']` 直接到终态。
- Android 同源令牌（`MainActivity.java`）：`MOTION_FEEDBACK 120` / `MOTION_SURFACE 150` / `MOTION_OPEN 250` / `MOTION_ENTER 320` / `MOTION_FLIGHT 400`，所有 `setDuration` 只引用这些常量；系统「移除动画」（Animator duration scale = 0）时 ViewPropertyAnimator 自动瞬时完成。
- 动效偏好两端一致：新文字淡入（淡入 / 直接出现）、消息气泡动画（原地长出 / 从输入框飞上来）、聊天区滚动动画（跟随滑动 / 直接跳到）。桌面写 `html[data-*]` + CSS；Android 在设置 → 外观 → 动效用三段滑块写 `SharedPreferences`，不重建 Activity，下一条消息/下一次滚动即生效（详见 `docs/ui-acceptance.md` §4b）。

### 1.3 对比度（WCAG AA：文字 ≥ 4.5:1，图标 ≥ 3:1）

Claude 色卡全部组合实测通过（数值为 WCAG 2.x 计算结果）：

| 场景 | 组合 | 对比度 |
|---|---|---|
| 浅色正文 | #161616 on #FFFFFF | 18.10 |
| 浅色次要 | #3D3D3A on #FFFFFF | 10.90 |
| 浅色辅助 | #6B6B66 on #FFFFFF / on #FAFAFA | 5.36 / 5.13 |
| 浅色白字主按钮 | #FFFFFF on #B4562F | 4.87 |
| 浅色白字危险按钮 | #FFFFFF on #C4453A | 4.93 |
| 浅色强调文字 | #9C4527 on #FFFFFF | 6.37 |
| 深色正文 | #FDFDFD on #191919 | 17.28 |
| 深色次要 | #B8B5AD on #191919 | 8.58 |
| 深色辅助 | #8F8C85 on #191919 | 5.24 |
| 深色白字主按钮 | #FFFFFF on #B34D2A | 5.23 |
| 深色强调文字 | #E08A6D on #191919 | 6.72 |

头像不再用饱和渐变 + 白字：底色为 `color-mix(in srgb, var(--avatar-*-from) 18%, transparent)`，首字母用 `--text-ink-2`，与 Claude 的单色字母头像一致。8 种发送者名称色（`--name-0` … `--name-7`）在气泡上仍 ≥ 4.8:1。

### 1.4 Android（Java 端）色板映射

`MainActivity.java` 的常量与 `res/values/styles.xml` 同步为同一套色值（2026-11）：

| 常量 | 值 | 对应桌面令牌 |
|---|---|---|
| `INK` | #161616 | `--text`（浅色） |
| `MUTED` | #6B6B66 | `--text-3`（浅色） |
| `ACCENT` | #B4562F | `--accent-strong`（浅色，白字 4.87:1） |
| `TINT` | #F2F0EC | `--bg-hover`（浅色） |
| `PAPER` | #FFFFFF | `--bg`（浅色） |
| `WHITE` | #FAFAFA | `--bg-sidebar`（浅色） |
| `OUTLINE` | #E6E4DD | 描边 |
| `DANGER` | #C4453A | `--danger`（浅色） |

- `styles.xml`：colorAccent #B4562F、colorBackgroundFloating #FFFFFF、windowBackground #FFFFFF。
- 全屏图片底、状态栏/导航栏（全屏查看时）#161616；对话框遮罩 60% 黑；recycler ripple 用 #B4562F 低透明度。
- 深色模式（2026-11-25）：跟随系统 uiMode（`applyPalette()` 在 `onCreate` 建视图之前执行；系统切换深浅时 Activity 重建）。深色取值对齐桌面 `tokens.css` 深色：`INK #F5F4EF`、`MUTED #A3A099`、`PAPER #191919`、`WHITE #1F1F1F`、`TINT #242422`、`OUTLINE #3A3936`、`DANGER #E0655A`。强调色拆成两个变量：`ACCENT #E08A6D`（深色下用于文字/图标，在 #191919 上 6.9:1）与 `ACCENT_FILL #B4552F`（主按钮、未读角标填充，白字 4.87:1）。自己的气泡独立成 `BUBBLE_SELF`：浅色 #B4562F + 白字（品牌色块），深色 #2B2B2A + #F5F4EF 字（Claude 式中性块）。
- 主题资源：`res/values-night/styles.xml`（parent `Theme.Material.NoActionBar`、windowBackground #191919、colorAccent #E08A6D）；代码里再按 DARK 设状态栏/导航栏为 PAPER 并切换 system bar 图标的明暗（`newPage` 的 inset listener）。
- 圆角、卡片、dock 结构与字体（Cascadia Next SC）保持原样；Android 端不提供 App 内手动切换，只跟随系统。
- 2026-11-25 真机验收（Xiaomi 14 Pro，1440×3200 @560dpi，APK 0.4.0 / versionCode 16）：画布实测 #FEFEFE、页头 #FAFAFA、列表卡与按钮 #F2F0EC、自己的气泡 #A85B39 白字（≥4.5:1）、对方气泡 #FFFFFF 深字、输入条 #FFFFFF。对照图：`.tools/shots/android-login.png`、`android-chat.png`、`android-contacts.png`。

### 1.5 字体（明确保留）

- 桌面：`Cascadia Code`（UI 正文）+ `Cascadia Next SC`（中文，unicode-range U+2E80–303F 等回退 Microsoft YaHei UI）。Android：`assets/CascadiaNextSC.wght.ttf`。
- 未使用 Anthropic Sans（未随参考仓库提供、无授权）。官方 Claude 截图的字宽/字形与本地存在可见差异，属已知差距，不替换。

## 2. 布局

- 三栏：会话列表 260px（可拖动 240–380px，宽度存于 `localStorage['lan-chat.sidebarWidth']`）、聊天区、信息面板 340px（默认收起）。
- 窗口宽度 < 900px：信息面板改为浮层，背后加遮罩。< 700px：列表和聊天交替显示，聊天头部有返回按钮。
- Windows 窗口保留系统标题栏。窄窗口在会话列表和聊天区间切换。

## 3. 组件

| 区域 | 文件 | 要点 |
|---|---|---|
| 基础组件 | `components/ui/*` | Button、IconButton（必须有 aria-label 和 tooltip）、Avatar、Dialog（16px 圆角，缩放 .96→1）、DropdownMenu / ContextMenu、Switch / SegmentedRadio / Select、Skeleton / EmptyState / ErrorState、Toaster（右下角，3s） |
| 会话列表 | `components/sidebar/*` | 汉堡菜单、胶囊搜索框（≥ 6 个会话时显示）、全部 / 群聊 / 私聊共用一个左右滑动的选中滑块（默认群聊）、56px 行、32px 头像、99+ 角标、在线点、虚拟滚动、空状态提供“创建或加入频道” |
| 聊天头部 | `components/chat/ChatHeader.tsx`、`ConnectionBanner.tsx` | 52px，副标题“N 名成员，M 人在线”；搜索、审批角标（数量增加时弹跳）、更多菜单；重连横幅 |
| 消息列表 | `components/chat/MessageList.tsx` | 底部锚定、虚拟化、回到底部按钮带新消息数、按实际本地日期显示分隔线、“以下为新消息”分隔线；点击引用跳到原消息并高亮 1s |
| 气泡 | `components/chat/MessageBubble.tsx`、`RichText.tsx`、`FileCard.tsx` | 最大宽度为 560px 与可用宽度 78% 中较小者；发送时间和送达状态位于气泡外上方，随气泡左右对齐；支持链接、代码、路径、悬停操作条与右键菜单 |
| 输入框 | `components/composer/*` | 自动增高 1–6 行；Enter 发送、Shift+Enter 换行；输入法上屏的回车不发送；Esc 取消回复；草稿按会话保存；emoji-mart 懒加载；有文字时才出现发送按钮 |
| 面板与弹窗 | `components/shell/*`、`components/dialogs/*` | 信息面板、宽度拖动条、审批弹窗（60 秒倒计时环）、新建 / 管理频道（删除需要二次确认）、成员资料、管理操作确认 |
| 页面 | `app/*` | 工作区、设置（账户 / 外观 / 通知 / 隐私与安全 / 关于）、模式选择与连接页、错误页 |

### 3.1 入口页（模式选择 / 登录）

入口页不套卡片壳，内容直接在画布上居中，最大宽度按内容分两档：模式选择 400px、登录 / 注册 380px；窄屏左右各 24px 内边距，任何宽度下都不出现横向滚动。

| 项 | 规格 |
|---|---|
| 品牌行 | 44px 品牌图标（12px 圆角，不加边框）+ 20px semibold 标题；模式页标题为「LAN Chat」，副标题「本地网络聊天 · 端到端加密」（12px、text-ink-2），登录页只有「登录 / 创建账号」 |
| 安全要点 | 仅模式页；14px 线性图标 + 12px text-ink-2，三行分别是 TLS 传输 · 证书指纹校验、MLS 端到端加密频道与附件、房主审批每一次成员连接 |
| 模式列表 | 容器 12px 圆角、0.5px divider 描边、bg-sidebar；每行最小 58px、左右 14px、hover 换 bg-hover、行间 0.5px 分隔线；左侧 15px 图标，右侧 15px 箭头；点击区域为整行。局域网版只渲染「创建本地聊天室」「加入局域网聊天室」两行，在线版渲染四行 |
| 表单 | 输入框 h-10、8px 圆角、0.5px 描边、bg-input，聚焦只换成 accent 描边（不新增外环）；标签 12px text-ink-2，字段之间 12px |
| 按钮 | 主按钮通栏 h-9；禁用态为 bg-input + text-ink-3 的灰面（不使用半透明品牌色），其余变体禁用仍用 50% 透明度；「注册账号 / 返回登录」是通栏 ghost 按钮 |
| 文案 | 不再出现「在线版」「在线账号」字样：登录页标题为「登录」，注册页标题为「创建账号」 |

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
- 私聊的在线状态只能从 `state.members` 推断。
- 引用跳转按“作者 + 原文”匹配，协议中没有被引用消息的 ID。
- 拖入或粘贴文件只会打开原生文件选择窗口：按附件安全约定，页面不能读取本地文件。
- 图片缩略图（依赖 spec 03）、取消上传（没有对应命令）均未实现。
- 桥接一次最多下发 500 条消息（`kMaxSerializedRows`）。
- 设置中的“通知”显示为“暂未开放”。
