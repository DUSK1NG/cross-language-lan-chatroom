# LAN Chat 双端视觉与动效验收（2026-11-25）

对照基准：[dsh-claude-style](https://github.com/Nwflower/dsh-claude-style)（MIT）的 Claude 色卡与 Claude motion；
桌面端令牌唯一来源 `frontend/src/styles/tokens.css`，Android 端为 `MainActivity.java` 的 `applyPalette()`。

## 1. 色板映射

| 语义 | 桌面（深 / 浅） | Android（深 / 浅） |
|---|---|---|
| 画布 | #191919 / #ffffff | `PAPER` #191919 / #FFFFFF |
| 面（卡片、页头） | #1f1f1f / #fafafa | `WHITE` #1F1F1F / #FAFAFA |
| 次级面 / 悬停 | #242424 / #f2f0ec | `TINT` #242422 / #F2F0EC |
| 正文 | #fdfdfd / #161616 | `INK` #F5F4EF / #161616 |
| 辅助文字 | #b8b5ad / #6b6b66 | `MUTED` #A3A099 / #6B6B66 |
| 描边 | 1px rgba 白/黑 | `OUTLINE` #3A3936 / #E6E4DD |
| 强调（文字/图标） | #d97757 | `ACCENT` #E08A6D / #B4562F |
| 强调（填充） | — | `ACCENT_FILL` #B4552F（白字 4.87:1） |
| 自己的气泡 | #262626 中性块 | `BUBBLE_SELF` #2B2B2A + #F5F4EF / 浅色 #B4562F + 白字 |
| 危险 | #f87171 系 | #E0655A / #C4453A |

## 2. 动效映射

| 场景 | 桌面令牌 | Android 常量 |
|---|---|---|
| 按压反馈 | `--dur-press` 80ms | 由 Ripple / ViewPropertyAnimator 承担 |
| hover / 退出 | `--dur-hover`、`--dur-exit` 120ms | `MOTION_FEEDBACK` 120 |
| 浮层 / 面板 | `--dur-panel` 250ms | `MOTION_OPEN` 250 |
| 入场 | `--dur-enter` 320ms | `MOTION_ENTER` 320 |
| 发送飞入 | `--dur-send` 400ms | `MOTION_FLIGHT` 400 |
| 缓动 | `--ease` cubic-bezier(.22,1,.36,1)，支持时切 `linear()` 弹簧 | `DecelerateInterpolator(1.6f)`（ARRIVE） |
| 系统 Animator duration scale = 0 | — | 动画直接到终态 |
| 省电档 | `html[data-effects='off']` 直接终态 | — |

## 3. 已验证页面（截图）

- 桌面深色工作区：`.tools/shots/restore-dark.png`
- 桌面浅色工作区：`.tools/shots/m-workspace-light.png`
- 桌面深色设置：`.tools/shots/m-settings-dark.png`
- 桌面窄屏 760：`.tools/shots/m-760.png`；窄屏 440：`.tools/shots/m-440.png`
- Android 浅色：`android-login.png`、`android-chat.png`、`android-contacts.png`、`light-chat3.png`
- Android 深色：`dark-list.png`、`dark-chat2.png`、`dark-contacts.png`
- Android 弹窗（深 / 浅）：`dark-dialog2.png`、`light-dialog2.png`

## 4. 已知差异

1. Android 的主题开关在「设置 → 外观」的三档滑块里，选中后重建 Activity 生效并仍停留在设置页；桌面在「设置 → 外观 → 外观主题」。
2. 深色下 Android 自己的气泡用中性块，浅色保留品牌 terracotta；桌面两端都是中性块。属有意保留的品牌差异。
3. 全屏看图弹窗固定深底 #161616，不随主题切换。
4. Android 输入框在深色下的光标/选区颜色走主题 colorAccent（#E08A6D），与 Android 系统组件行为一致。
5. 字体未替换：桌面 Cascadia Code + Cascadia Next SC，Android Cascadia Next SC；Anthropic Sans 未授权、未随参考仓库提供。

## 4b. 动效偏好（设置 → 外观 → 动效）

与 dsh-claude-style 的 Claude Style 面板同义，落在桌面端：

| 设置项 | 选项 | 落地方式 |
|---|---|---|
| 新文字淡入 | 淡入 / 直接出现 | 默认：`.message[data-entering] .message-content { animation: cirrus-text-in var(--dur-enter) }`，延迟 60ms 由 0 → 1；`instant` 档用 `animation: none !important` 整块关掉（长回复的逐词 `.cirrus-word` 同受影响） |
| 消息气泡动画 | 原地长出 / 从输入框飞上来 | `data-bubble-motion='grow'` 换成 `cirrus-grow`；默认 `fly` 走 `--send-x/--send-y` |
| 聊天区滚动动画 | 跟随滑动 / 直接跳到 | `scrollTo`/`scrollToIndex` 的 behavior 由 `settings.scrollMotion` 决定 |

偏好存 `lan-chat.settings`，与主题/强调色同一份；`useAppearanceAttributes` 把三个值镜像到 `<html data-*>`。

Android 端同形制落在设置 → 外观 → 动效，三项都是三档滑块（TINT 轨道 + PAPER 胶囊，点击后 150ms 滑到选中项），
直接写 `SharedPreferences`（键 `motion_new_text` / `motion_bubble` / `motion_scroll`），取值为：

| 设置项 | 选项（默认在前） | Android 落地 |
|---|---|---|
| 新文字淡入 | 淡入 / 直接出现 | 气泡组初始 alpha：淡入 0.4 → 1；直接出现从 1 起步，只保留位移动画 |
| 消息气泡动画 | 从输入框飞上来 / 原地长出 | fly：下方 28dp 起、400ms（`MOTION_FLIGHT`）；grow：scale 0.94 → 1 + 上浮 4dp、320ms（`MOTION_ENTER`），与桌面 `cirrus-grow` 同参数 |
| 聊天区滚动动画 | 跟随滑动 / 直接跳到 | `ConversationScroll.followArrivals` 与 `followConversation` 的 preDraw 里，jump 时强制 `scrollTo` 而非 `smoothScrollTo` / 动画器 |

滑块抽出通用实现 `segmentedSlider(key, fallback, values, texts, afterChange)`，主题滑块复用同一条路径（afterChange 里 `recreate()`）。
偏好切换不重建 Activity，下一条消息/下一次滚动即生效。

截图：`.tools/shots/m-motion-settings.png`（桌面深色）、`.tools/shots/and-motion-settings-dark.png`、`.tools/shots/and-motion-settings-light.png`、
`.tools/shots/and-motion-settings-alt.png`（三项都切到非默认）、`.tools/shots/and-motion-frame-a.png`（fly 动画进行中）。

## 4c. 输入法（IME）组字

拼音在候选条里、还没上屏时，输入框里显示的字符属于「组字区间」，不能当成消息内容。两端都加了守卫：

| 端 | 位置 | 行为 |
|---|---|---|
| 桌面 | `frontend/src/components/composer/Composer.tsx` | `composingRef` + `onCompositionStart/End`；组字期间点「发送消息」或按 Enter 都不发送 |
| Android | `MainActivity.java` 发送按钮 | `BaseInputConnection.getComposingSpanStart(...) >= 0` 时调 `InputMethodManager.viewClicked()` 让 IME 提交候选词后直接 return，不把拼音当消息发出 |
| Android | 草稿持久化 `TextWatcher` | 组字区间不写 `SharedPreferences` 草稿，避免重进会话看到半截拼音 |

实测（CDP `Input.imeSetComposition`）：组字态 textarea 值为 `nei'r`，点发送后消息数不变、输入框内容保留（`.tools/shots/ime-fix-1-composing.png`、`ime-fix-2-after-click.png`）；提交候选词「内容」后点发送才发出（`ime-fix-3-committed-send.png`）。
Android 端无法用 adb 注入组字（`input tap/keyevent` 驱动不了 Sogou 候选条），只做代码级校验。

## 4d. 动效开关的可见性证据

`.tools/anim.mjs` 通过 preview 暴露的 fake bridge `publish()` 推一条新消息，分别在 90ms / 790ms 取帧与计算样式：

| 档位 | `animationName` | 90ms 时正文 opacity | 终值 |
|---|---|---|---|
| 淡入 | cirrus-text-in | 0.73 | 1 |
| 直接出现 | none | 1 | 1 |

截图：`.tools/shots/anim-fade-early.png`、`anim-fade-late.png`、`anim-instant-early.png`、`anim-instant-late.png`。

## 5. 复验命令

```bash
cd frontend && npm run typecheck && npx vitest run && ./node_modules/.bin/vite build
# Android（本机自带工具链）
export JAVA_HOME=.../.tools/android/jdk ANDROID_HOME=.../.tools/android/sdk GRADLE_USER_HOME=.../.tools/android/gradle-home
MSYS2_ARG_CONV_EXCL='*' MSYS_NO_PATHCONV=1 "$JAVA_HOME/bin/java.exe" -Xmx1500m \
  -classpath .../.tools/android/gradle/lib/gradle-launcher-8.13.jar \
  org.gradle.launcher.GradleMain --console=plain assembleDebug
# 主题切换验证
adb shell cmd uimode night yes|no
```
