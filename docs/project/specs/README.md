# 升级路线图（1.3.0 → 1.4.0）

| 顺序 | 规格 | 版本 | 内容 | 决策门 |
|---|---|---|---|---|
| 1 | [01-stability-release](01-stability-release/) | 1.3.0 | 双机重连验收、下载链路加固、C++ 进 CI、依赖固定、tag 发布工作流、前端错误边界 | 4.1 MLS++ 打包与代码签名 |
| 2 | [02-design-system](02-design-system/) | 1.3.0 | 字体/间距/动效令牌、退出动画、方向性页面转场、会话交叉淡化、FLIP 工具 | — |
| 3 | [03-media-preview](03-media-preview/) | 1.4.0 | 图片/视频（≤ 100 MB）缩略图与内联预览、灯箱、媒体缓存与自定义 scheme、私信附件 | 1.1 不变量修订与缓存策略；1.2 视频方案 |
| 4 | [04-desktop-notifications](04-desktop-notifications/) | 1.4.0 | 托盘、系统通知、免打扰、频道静音、房主关闭到托盘 | — |

方法论：`docs/project/steering/engineering-methodology.md`；设计语言：`docs/project/steering/design-language.md`；执行代理：`docs/project/agents/sonnet-executor.md`（Claude Sonnet 5.5）。

## 如何执行
按任务编号执行：
> 使用 sonnet-executor 代理执行 `docs/project/specs/01-stability-release/tasks.md` 的任务 1.1

逐个子任务执行；遇到 ⛔ 决策门时代理会停下并给出决策简报，由你答复后继续。
