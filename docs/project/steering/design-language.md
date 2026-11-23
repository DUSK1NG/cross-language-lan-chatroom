---
inclusion: fileMatch
fileMatchPattern: "frontend/**"
---

# LAN Chat 设计语言

完整规范、令牌表与对比度核算见 `docs/ui-design-spec.md`。本文件只列每次改前端都必须遵守的规则。

连接、聊天和设置共享一套背景与磨砂层次。气泡沿用 [Vibe Design Tips 的 Cirrus](https://vibedesign.tips/012-message-bubbles/)，时间和送达状态放在气泡上方。

## 1. 令牌
- 唯一来源 `frontend/src/styles/tokens.css`；组件通过 Tailwind 工具类（`app.css` 的 `@theme inline` 映射）或 `var()` 引用。禁止颜色字面量（ESLint 会拦截），新颜色先加令牌并写明对比度。
- 文字对比度 ≥ 4.5:1，图标/非文字 ≥ 3:1；深色、浅色与 5 个强调色预设都要核算（`html[data-accent]`）。
- 沿用令牌的字号、圆角和间距，按消息分组与阅读层级组织聊天区。

## 2. 组件
- 只使用 `components/ui/*` 的基础组件（Radix 提供语义与焦点管理）；图标统一 lucide-react。
- 纯图标按钮必须用 `IconButton`（aria-label + tooltip）。所有操作反馈用 toast 或就地 `CommandFeedback`，禁止 `alert()`。
- 列表可能超过百行时用 @tanstack/react-virtual；行组件用 `React.memo`。

## 3. 动效
- 新发送的气泡从输入区方向进入，菜单按触发位置展开。切换会话筛选时，选中底板移动到当前标签。参照 [界面连续性](https://vibedesign.tips/015-ui-continuity/)。
- 入场减速、退场加速、位置变化平滑过渡。气泡动画期间保持正文排版。参照 [缓动曲线](https://vibedesign.tips/011-easing-curves/)。
- 动效服从应用的性能档位。历史消息不重放入场动画。

## 4. 不可越界
- 不改桥接命令、状态结构、Go 服务端。协议缺失的能力（已读、正在输入、表情回应等）只做降级，并在 `docs/ui-design-spec.md` §5 登记。

## 5. 验收
- `pnpm lint`、`pnpm typecheck`、`pnpm test -- --run`、`pnpm build` 全部通过；行为测试不得删除，只能随界面改写。
- 视觉改动在 PR 中附深色、浅色以及 820px / 600px 截图，并说明对比度。
