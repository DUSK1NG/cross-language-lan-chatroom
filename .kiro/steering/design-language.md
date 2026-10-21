---
inclusion: fileMatch
fileMatchPattern: "frontend/**"
---

# 设计语言：舒适的字体 · 适当的间距 · 丝滑的动画与转场

所有前端改动遵循本文件。令牌唯一来源是 `frontend/src/styles/tokens.css`；不得在组件 CSS 中写裸数值（颜色、字号、间距、时长、缓动），新需求先加令牌。

## 1. 字体（舒适、可读）
- 字体栈：`"Segoe UI Variable Text", "Segoe UI", "Microsoft YaHei UI", "PingFang SC", "Noto Sans SC", sans-serif`；数字用 `font-variant-numeric: tabular-nums`（时间、计数、进度、文件大小）。
- 字号阶梯（只用这些）：`--t-text-xs 11.5px` / `--t-text-sm 12.5px` / `--t-text-md 14px`（正文、消息）/ `--t-text-lg 16px` / `--t-text-xl 20px` / `--t-text-2xl 26px`。
- 行高：正文与消息 1.6；标题 1.3；单行控件 1.4。中文段落不使用负字距；标题允许 `-0.01em`。
- 字重只用 400 / 550 / 650 / 750。层级优先靠字号与颜色（`--t-ink` / `--t-ink-2` / `--t-ink-3`），少用加粗。
- 正文对比度 ≥ 4.5:1，次要文字 ≥ 3:1（浅色与深色两套都要满足）。

## 2. 间距（4px 网格）
- 间距阶梯：`--t-space-1 4px` `-2 8px` `-3 12px` `-4 16px` `-5 20px` `-6 24px` `-8 32px` `-10 40px` `-12 48px`。
- 组件内部用 1–3，组件之间用 3–5，区块/页面边距用 6–12。同一列表内的项间距必须一致。
- 可点击目标最小 32×32px（主要按钮 36px 高），相邻可点击目标间距 ≥ 4px。
- 圆角：`--t-radius-sm 8` / `--t-radius 12` / `--t-radius-lg 18`；嵌套元素的圆角 = 外层圆角 − 内边距（不小于 4px）。
- 内容最大宽度：消息气泡 `min(72%, 680px)`；设置/表单面板 ≤ 680px；长文本 ≤ 68 字符宽。

## 3. 动画（丝滑、克制）
- 时长令牌：`--t-dur-instant 100ms`（按下反馈）/ `--t-dur-fast 160ms`（悬停、颜色）/ `--t-dur-base 240ms`（展开、抽屉、列表项）/ `--t-dur-slow 360ms`（页面、弹窗）/ `--t-dur-hero 520ms`（首屏、发行版卡片）。所有时长乘以 `--t-motion-scale`（原生性能档位）。
- 缓动令牌：`--t-ease-standard cubic-bezier(0.2, 0, 0, 1)`（默认）/ `--t-ease-out cubic-bezier(0.22, 1, 0.36, 1)`（进入）/ `--t-ease-in cubic-bezier(0.4, 0, 1, 1)`（退出，时长 ×0.75）/ `--t-ease-spring cubic-bezier(0.34, 1.4, 0.64, 1)`（弹出、徽标，只用于 ≤ 2 个元素）。
- 只动画 `transform` 与 `opacity`；禁止动画 `width/height/top/left/box-shadow/filter/background-position`。需要尺寸变化时用 FLIP（先测量再用 transform 过渡）。
- 进入动画从 4–16px 位移 + 透明度开始；退出比进入快且位移更小。列表逐项入场间隔 40–60ms，最多 12 项，其余直接显示。
- 循环动画仅限：加载指示、可见状态下的卡片装饰；卡片关闭或离屏时必须停止。
- 双重门控必须保留：`html[data-effects='off']` 与 `prefers-reduced-motion: reduce` 下所有动画与过渡直达终态（`tokens.css` 末尾规则）。新动画用 GSAP 时必须经 `motionAllowed()`。

## 4. 转场（空间连续性）
- 页面切换（模式/连接/房主/设置/工作区）：优先使用同文档 View Transitions（`document.startViewTransition`，Qt 6.11 的 Chromium 支持）；不可用或门控关闭时退回现有 `animatePage`。方向语义：前进 = 新页从右 24px 滑入，返回 = 从左。
- 共享元素：从缩略图打开图片/视频灯箱时，用 FLIP 从缩略图位置放大到全屏（`--t-dur-slow`，`--t-ease-out`），关闭时反向回到原位；缩略图已不在视口时退化为淡出缩放。
- 会话切换：时间线交叉淡化（旧内容 opacity → 0，120ms；新内容自下 8px 进入，240ms），不重放已显示消息的入场动画。
- 弹窗/抽屉：背景遮罩淡入 `--t-dur-fast`；面板 `--t-dur-slow` + `--t-ease-out` 进入，`--t-ease-in` 退出；退出动画完成后再卸载（需要退出态的组件使用 `data-state="closing"`）。
- 主题切换：仅在用户操作时短暂开启颜色交叉淡化（`html.theme-transition`，已实现）。

## 5. 性能预算
- 交互 P95 帧耗时 ≤ 16.7ms（60Hz 参考机）；任何新动画不得让 `frontend/src/performance` 遥测的 P95 恶化超过 1ms。
- 每屏同时运行的循环动画 ≤ 3 个；`backdrop-filter` 只用于遮罩层且在 `data-effects='off'` 下关闭。
- 图片缩略图固定宽高比占位，加载完成不得引起布局跳动（CLS = 0）。

## 6. 验收方式
每个视觉/动效改动在 PR 中附：浅色、深色、820px 窄屏截图；动效改动附关键帧采样（如 Playwright 读取 `getComputedStyle(...).transform` 的 3 个时间点）以及 reduced-motion 下 `document.getAnimations().length === 0` 的证据。
