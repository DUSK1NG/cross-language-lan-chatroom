# 02 设计系统 — 任务

> **已被取代（2026-10-21）**：前端已按 Telegram Desktop 深色风格重做（Tailwind + Radix + Framer Motion，移除 GSAP），现行规范见 `docs/ui-design-spec.md` 与 `.kiro/steering/design-language.md`。本规格中引用的 `animation/motion.ts`、`MessageTimeline.tsx` 等文件已不存在，仅作历史记录。

前置：规格 01 的任务 1、2 已合并（CI 中能跑前端检查）。执行规则见 `.kiro/steering/engineering-methodology.md`。

- [ ] 1. 令牌与检查工具（分支 `feat/02-design-tokens`）
  - [ ] 1.1 在 `tokens.css` 增加排版、间距、时长、缓动令牌；`--t-fast` 保留为别名。_R1.1 R2.1 R3.1_
  - [ ] 1.2 `frontend/scripts/lint-tokens.mjs` + 正反例自测；`package.json` 增加 `lint:tokens`，暂以「仅报告」模式接入 CI。_D2_
  - [ ] 1.3 `frontend/scripts/check-contrast.mjs` + 自测；修正不达标的令牌值并附前后截图。_R1.3_

- [ ] 2. 样式迁移（分支 `feat/02-design-migrate`，每个文件一个提交）
  - [ ] 2.1 `pages.css` 迁移到令牌；截图对比。_R1 R2_
  - [ ] 2.2 `workspace.css` 迁移；消息正文 14px/1.6，数字等宽。_R1.2_
  - [ ] 2.3 `motion.css`、`cards.css`、`release.css` 迁移到时长/缓动令牌。_R3.1_
  - [ ] 2.4 检查所有可点击目标 ≥ 32px（Playwright 遍历 `button, [role=button]` 的 `getBoundingClientRect`），修正不足者。_R2.2_
  - [ ] 2.5 `lint:tokens` 切换为阻断模式；删除 `--t-fast` 别名。_D2_

- [ ] 3. 动效基础设施（分支 `feat/02-motion-core`）
  - [ ] 3.1 失败测试 → 实现 `motionToken()`；`motion.test.ts` 原有断言保持通过。_D3_
  - [ ] 3.2 失败测试 → 实现 `usePresence()`。_R3.2_
  - [ ] 3.3 `ModalSurface`、成员抽屉、`EmojiPicker`、`ConnectionToast`、`ReleaseNotesCard` 接入退出动画；现有 132 个前端测试保持通过（必要时用 `vi.useFakeTimers` 推进退出时长，不得删除断言）。_R3.2_
  - [ ] 3.4 实现 `animateFlip()` + 单测（mock GSAP，断言起始 transform 计算正确）。_R4.3_

- [ ] 4. 转场（分支 `feat/02-transitions`）
  - [ ] 4.1 失败测试：导航方向计算（mode→connect = forward，settings→workspace = back）。实现方向与 `data-nav-direction`。_D4_
  - [ ] 4.2 View Transitions 路径 + GSAP 回退；在 Qt 6.11 WebEngine 实机确认 `startViewTransition` 可用（记录 `navigator.userAgent` 中的 Chromium 版本）。_R4.1_
  - [ ] 4.3 会话切换交叉淡化。_R4.2_

- [ ] 5. 验收门
  - [ ] 5.1 按 `requirements.md` 验收清单提交截图、关键帧采样与门控证据。_R5.1_
  - [ ] 5.2 在 Windows 参考机用现有帧遥测对比改动前后的 P95（各采 3 次取中位数），恶化 ≤ 1ms。_R5.2_

## 待评估
