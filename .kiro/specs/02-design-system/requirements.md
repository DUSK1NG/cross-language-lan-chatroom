# 02 设计系统：字体 · 间距 · 动画 · 转场 — 需求

> **已被取代（2026-10-21）**：前端已按 Telegram Desktop 深色风格重做（Tailwind + Radix + Framer Motion，移除 GSAP），现行规范见 `docs/ui-design-spec.md` 与 `.kiro/steering/design-language.md`。本规格中引用的 `animation/motion.ts`、`MessageTimeline.tsx` 等文件已不存在，仅作历史记录。

目标版本：1.3.0。依据 `.kiro/steering/design-language.md`。本规格不新增功能，只让现有界面在**排版舒适度、间距节奏与转场连续性**上达到一致。

## R1 排版令牌
- R1.1 THE frontend SHALL 定义字号、行高、字重令牌（design-language §1），所有组件 CSS 只引用令牌。
- R1.2 THE message text SHALL 使用 14px / 行高 1.6；时间、计数、进度、文件大小 SHALL 使用等宽数字。
- R1.3 THE frontend SHALL 在浅色与深色两套主题下满足正文 ≥ 4.5:1、次要文字 ≥ 3:1 的对比度（自动化检查）。

## R2 间距令牌
- R2.1 THE frontend SHALL 定义 4px 网格间距令牌，组件 CSS 中不再出现裸 `px` 间距（`0`、`1px` 边框除外）。
- R2.2 THE interactive targets SHALL ≥ 32×32px，主要按钮高度 36px。

## R3 动效令牌与退出动画
- R3.1 THE frontend SHALL 定义时长与缓动令牌（design-language §3），GSAP 与 CSS 共用同一组数值（`motion.ts` 从 CSS 变量读取或共享常量）。
- R3.2 WHEN 弹窗、抽屉、表情面板、连接提示关闭 THE SYSTEM SHALL 播放退出动画后再卸载；门控关闭时立即卸载。

## R4 转场
- R4.1 WHEN 在模式/连接/房主/设置/工作区之间切换 THE SYSTEM SHALL 使用方向感明确的页面转场（前进右入、返回左入），优先 View Transitions，不可用时退回 GSAP。
- R4.2 WHEN 切换会话 THE SYSTEM SHALL 交叉淡化时间线，且不重放已有消息的入场动画。
- R4.3 THE frontend SHALL 提供通用 FLIP 工具（`animateFlip(fromRect, toElement)`），供规格 03 的灯箱共享元素转场使用。

## R5 性能与门控
- R5.1 THE new motion SHALL 只动画 transform/opacity；`data-effects='off'` 与 reduced-motion 下 `document.getAnimations().length === 0`。
- R5.2 THE interaction P95 frame time SHALL 不比改动前恶化超过 1ms（用 `frontend/src/performance` 遥测在同一台机器上对比）。

## 验收清单
1. 浅色/深色/820px 截图：模式页、连接页、工作区、设置、成员资料卡、发行版卡片。
2. 页面前进与返回、会话切换、弹窗开关的关键帧采样（3 个时间点的 transform/opacity）。
3. reduced-motion 下无运行中的动画；`data-effects=off` 下同样。
4. 对比度检查脚本输出全部通过。
