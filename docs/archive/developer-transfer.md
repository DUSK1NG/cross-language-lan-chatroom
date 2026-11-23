# 开发迁移包（换电脑继续开发）

在主仓库根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-developer-transfer.ps1
```

产物：

```text
release\LANChat-Developer-Transfer-windows-x64.zip
```

它用于把项目带到另一台开发电脑，不是给普通聊天成员下载的运行包。

## 包含内容

- `LANChat-Source`：当前提交的完整干净源码、源码启动器和交接文档；
- `git-history.bundle`：所有本地 Git 引用与历史的离线 bundle；
- `Codex-optional\AGENTS.md` 与 `Codex-optional\skills`：可选的用户规则和
  用户安装技能（不含 Codex 自带 `.system` 技能）；
- `worktree-snapshots`：两处旧 worktree 的未提交代码补丁和指定的真实源码
  文件快照，供人工审阅。

## 刻意不包含

私钥、证书、数据库、Git/Codex 登录信息、聊天日志、会话、缓存、构建目录和
依赖目录都不会被复制。新电脑的房主首次创建本地聊天室时应生成自己的证书和
私钥；不要把旧房主私钥带到新电脑。

`glass-ui` worktree 中存在房主私钥，因此只通过 Git bundle 保留其已提交历史，
不复制该 worktree 的工作目录。

## 新电脑恢复

1. 解压 ZIP，先阅读 `LANChat-Source\docs\CODEX_HANDOFF.md`。
2. 在 `LANChat-Source` 中双击 `LANChat-Launcher.exe`，首次按提示安装/准备
   开发工具链。
3. 如需历史引用，在新仓库中审阅并导入 `git-history.bundle`；先创建分支，避免
   将旧补丁直接覆盖当前 `master`。
4. 如需恢复 Codex 习惯，手动复制包内 `AGENTS.md` 和选定的技能目录到新电脑
   的 Codex 用户目录；重新登录 Codex 与 GitHub，绝不复制旧 `auth.json` 或
   `config.toml`。

## 验证

打包脚本会在压缩前后自动调用：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-developer-transfer-package.ps1 `
  -PackageDirectory .\release\LANChat-Developer-Transfer-windows-x64 `
  -ArchivePath .\release\LANChat-Developer-Transfer-windows-x64.zip
```

校验器会确认必要文件存在，并拒绝敏感文件、证书、私钥、数据库、会话、日志、
缓存和构建目录。
