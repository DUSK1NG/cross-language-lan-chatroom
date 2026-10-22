---
inclusion: always
---

# 工程方法论（所有执行代理必须遵守）

## 1. 执行单元
- 以 `docs/project/specs/<NN-name>/tasks.md` 中的**单个子任务**为执行单元；一次只做一个，完成后勾选 `[x]` 并在任务下追加一行 `> 证据：<命令/测试名/截图路径>`。
- 规格执行顺序固定：`01-stability-release` → `02-design-system` → `03-media-preview` → `04-desktop-notifications`。后一个规格不得在前一个规格的「验收门」通过前开始。
- 标注 **⛔ 决策门** 的任务：停止执行，向用户陈述选项、取舍与推荐，等待明确答复后再继续。不得自行选择。

## 2. 测试先行（沿用本仓库既有约定）
1. 先写一个**会失败**的聚焦测试（Go `go test -run`、C++ QtTest/CTest、前端 `vitest --run <file>`），确认它因正确的原因失败。
2. 写最小实现使其通过。
3. 跑该层全量测试，再跑跨层门禁（见 §4）。
4. 只有纯样式改动可以不写单测，但必须提供截图证据（浅色 + 深色 + 窄屏 820px）。

## 3. 不可破坏的安全不变量
以下来自现有代码与交接文档，任何任务都**不得削弱**；若任务看起来需要突破，按决策门处理：
- 前端不接触**文件路径与密钥**（`frontend/src/components/composer/Composer.tsx` 的 `chooseUpload` 只派发命令，由 C++ 选择、加密、分块）。经用户 2026-10-21 批准修订：前端可以通过会话内有效、可作废的 `lanchat-media://` 不透明令牌显示已解密的媒体；明文只存在于当前用户应用缓存目录，文件名随机，默认在退出时清除（规格 03 D1）。
- manifest 只经 MLS 保护后传输，密钥永不进入 bridge/UI（`client-cpp/gui/src/attachments/attachment_manifest.hpp:9-10`）。
- 服务端永不获得明文文件名、MIME、缩略图或密钥（`server-go/attachment_store.go:47-48,74-75`）。
- 入站附件命令使用字段白名单（`docs/handover/2026-09-01-encrypted-attachments.md:24`）。
- 每个成员连接仍须房主批准；不得新增绕过审批的路径。
- 测试只使用临时数据目录，不触碰真实 `%LOCALAPPDATA%` 数据。

## 4. 门禁（每个 PR 合并前）
| 层 | 命令 | 运行环境 |
|---|---|---|
| Go | `go test ./... && go test -race ./... && go vet ./...`（`server-go/`） | Linux/CI |
| 前端 | `pnpm install --frozen-lockfile && pnpm test -- --run && pnpm run build`（`frontend/`） | Linux/CI |
| C++ | `scripts/build-modern.ps1 -Action Test`（存在 `.tools/mlspp` 时脚本自动以 `-DLAN_CHAT_ENABLE_MLSPP=ON` 构建，`build-modern.ps1:280-283`；附件相关任务必须在 MLS++ 开启的构建上跑一次） | Windows（本地或 CI，见规格 01） |
| 人工 | 对应规格 `requirements.md` 末尾的验收清单 | 两台 Windows 电脑 |

沙箱/Linux 环境无法运行 C++ 门禁时，必须在 PR 描述中明确写出「C++ 未在本环境验证」，不得声称已通过。

## 5. 提交与 PR
- Conventional Commits，作用域沿用历史：`feat(attachments): …`、`fix(server): …`、`ci: …`、`docs: …`。
- 一个规格的一个阶段 = 一个分支 + 一个 PR；分支名 `feat/<spec>-<phase>`。PR 描述包含：做了什么、证据、未验证项、回滚方式。
- 不做未列入任务的重构；发现额外问题时追加到对应规格 `tasks.md` 末尾的「待评估」小节，而不是顺手修。

## 6. 版本
- `1.3.0`：规格 01 + 02（界面与稳定性）。
- `1.4.0`：规格 03 + 04（媒体预览、私信附件、桌面通知）。
- 版本号唯一来源：`installer/LANChat.iss`、`scripts/package-installer.ps1`、`frontend/src/changelog/releaseNotes.ts` 三处同步修改。
