# 02 设计系统 — 设计

## D1 令牌（`frontend/src/styles/tokens.css`）
在 `:root` 增加（数值见 `.kiro/steering/design-language.md`）：
```css
--t-text-xs … --t-text-2xl;  --t-leading-body: 1.6; --t-leading-title: 1.3; --t-leading-ui: 1.4;
--t-weight-regular: 400; --t-weight-medium: 550; --t-weight-semibold: 650; --t-weight-bold: 750;
--t-space-1 … --t-space-12;
--t-dur-instant/fast/base/slow/hero: calc(<ms> * var(--t-motion-scale));
--t-ease-standard/out/in/spring;
```
保留现有 `--t-fast` 作为 `--t-dur-fast` 的别名，迁移完成后删除。

## D2 迁移方式
- 逐文件迁移：`pages.css` → `workspace.css` → `motion.css` → `cards.css` → `release.css`。每个文件一个提交，提交前后截图对比；允许为对齐 4px 网格产生 ≤ 2px 的视觉变化，其余必须像素一致。
- 新增 `frontend/scripts/lint-tokens.mjs`：扫描 `src/styles/*.css`（`tokens.css` 除外），出现裸 `font-size`/`margin`/`padding`/`gap`/`transition-duration`/`animation-duration` 数值即报错（允许 `0`、`1px`、`100%`、`calc(var(--…))`）。接入 `pnpm run build` 之前的 `pnpm run lint:tokens`，CI 运行。
- 对比度：`frontend/scripts/check-contrast.mjs` 解析 `tokens.css` 两套主题，按 WCAG 公式检查 `ink/ink-2/ink-3` 对 `surface/wall/sidebar/bubble-in`，以及 `bubble-out-ink` 对 `bubble-out`。不达标则调整令牌值。

## D3 动效共享（`frontend/src/animation/motion.ts`）
- 新增 `motionToken(name)`：从 `getComputedStyle(document.documentElement)` 读取 `--t-dur-*`（转为秒）与缓动；在 jsdom 中返回默认值。现有 `animate*` 函数改为使用它，保持 `motion.test.ts` 中对 workspace 入场的断言不变（0.16s 对应 `--t-dur-fast`）。
- 新增 `usePresence(open, exitMs)` hook：返回 `{ mounted, state: 'open' | 'closing' }`；`open` 变 false 时先置 `closing`，`exitMs` 后卸载；`motionAllowed()` 为 false 时立即卸载。`ModalSurface`、成员抽屉、`EmojiPicker`、`ConnectionToast`、`ReleaseNotesCard` 使用它，并以 `data-state="closing"` 驱动退出 keyframes。
- 新增 `animateFlip(from: DOMRect, to: HTMLElement, options)`：计算缩放与位移，用 GSAP 从 `transform: translate()+scale()` 过渡到 `none`；返回 cleanup。

## D4 页面转场（`frontend/src/app/App.tsx`）
- 维护页面顺序 `mode < connect|host < workspace < settings`，导航时得出 `direction = 'forward' | 'back'`，写入 `document.documentElement.dataset.navDirection`。
- 如果 `motionAllowed()` 且存在 `document.startViewTransition`：在 `flushSync(() => setPage(next))` 外包一层 `startViewTransition`；CSS 中定义 `::view-transition-old(root)` / `::view-transition-new(root)` 的方向性 keyframes（24px 位移 + 透明度，`--t-dur-slow`，`--t-ease-out`）。
- 否则沿用现有 `usePageEntrance`（GSAP），方向同样生效。
- 连接成功自动进入工作区（`App.tsx` 中的 effect）也走同一转场函数。

## D5 会话切换（`MessageTimeline.tsx`）
- 以 `conversationKey` 作为 key 的外层容器在切换时：旧内容立即 `opacity: 0`（120ms），新内容 `translateY(8px) → 0`（240ms）。`enteringMessageIds` 逻辑已保证首屏消息不逐条入场，保持不变。

## 测试
- vitest：`usePresence` 的三种路径（打开、关闭等待、门控关闭立即卸载）；`App` 导航方向计算；`lint-tokens` 与 `check-contrast` 的正反例。
- Playwright 采样脚本（沿用 PR #12 的方法，放在 `frontend/scripts/visual/`，不进 CI，仅用于 PR 证据）。
