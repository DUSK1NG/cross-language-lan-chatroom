# 01 稳定性与发布 — 需求

目标版本：1.3.0。范围：让已有功能在真实双机环境可靠，让每次发布可重复、可验证、可追溯。

## R1 断线重连验收闭环
**背景**：`docs/known-issues.md` 记录的「批准后重连失败」已在 `server-go/hub.go:507-517` 修复并由 `TestApprovedReconnectReplacesStaleActiveSession`（`server-go/auth_store_test.go:484`）覆盖，但双机验收未做。
- R1.1 WHEN 已加入的成员网络中断后恢复 THE SYSTEM SHALL 在房主批准后让该成员回到原会话，且旧会话不再占用用户代码。
- R1.2 WHEN 成员改用新的用户代码连接 THE SYSTEM SHALL 让房主稳定收到新的待批准请求。
- R1.3 WHEN 重连成功 THE SYSTEM SHALL 保留当前会话、草稿，并补拉断线期间的消息（与 `ConnectionToast` 文案一致）。

## R2 附件下载链路加固（规格 03 的前置条件）
**背景**：`attachment_download_assembler.cpp` 直接写入用户选择的最终文件、失败不清理；下载为单块串行（`gui_connection_worker.cpp:932-956`）；发送者无法下载自己的附件（manifest 未本地保存）；前端不认识 `completed` 事件。
- R2.1 WHEN 下载失败或被取消 THE SYSTEM SHALL 删除未完成的输出，不留下预分配大小的残缺文件。
- R2.2 THE SYSTEM SHALL 以有界窗口（默认 8 块在途）并行下载，并按序写入；100 MB 文件在千兆局域网下载用时 ≤ 15 s。
- R2.3 WHEN 发送者完成上传 THE SYSTEM SHALL 在本地保留自己的 manifest，使发送者可以下载/预览自己的附件。
- R2.4 THE SYSTEM SHALL 通过 bridge 发布下载进度与完成/失败状态，时间线附件卡显示「接收中 n%」「已保存」「失败可重试」。

## R3 持续集成覆盖 C++
- R3.1 THE CI SHALL 在每个 PR 上构建并运行不依赖 WebEngine 的 C++ 单元测试（协议、加密、manifest、组装器、重连策略、bridge 协议校验）。
- R3.2 THE CI SHALL 在 Windows 上完成 GUI 目标的完整构建（可按需缓存 Qt），失败时阻止合并。
- R3.3 THE CI SHALL 不需要任何私钥、证书或真实数据。

## R4 可重复构建与发布
- R4.1 THE frontend SHALL 使用精确版本依赖（不再出现 `"latest"`），升级通过显式 PR 完成。
- R4.2 THE release SHALL 由打 tag（`v*`）触发的工作流产出 `LANChat-Setup-x64.exe`、`LANChat-Windows-x64.zip` 与 `SHA256SUMS.txt`，并把 `docs/release-notes/v<version>.md` 作为发行说明正文。
- R4.3 THE release workflow SHALL 自动检查包内不含 `.key`、`.crt`、`.db`、源码与构建工具（沿用 `scripts/test-member-package.ps1` 的规则）。
- R4.4 ⛔ IF 用户提供代码签名证书 THEN THE release SHALL 对 exe 与安装包签名；否则发行说明注明「未签名，请核对 SHA256」。
- R4.5 THE version number SHALL 在三处来源保持一致，并由一个检查脚本在 CI 中验证。

## R5 运行时健壮性
- R5.1 WHEN 前端渲染抛出异常 THE SYSTEM SHALL 显示可恢复的错误页（重试 / 返回模式选择），而不是白屏。
- R5.2 THE frontend SHALL 不在控制台产生未处理的 Promise 拒绝；bridge 断开时显示既有的 `BridgeUnavailablePage`。

## 验收清单（人工，两台 Windows 电脑）
1. 房主 A 创建聊天室；成员 B 发现并加入，A 批准。
2. 拔掉 B 的网线 30 s 后恢复：B 自动重连，A 再次批准，B 回到原频道，草稿仍在，断线期间 A 发的消息出现。
3. B 关闭客户端，用新用户代码重新加入：A 收到新请求。
4. A 发送 100 MB 文件，B 下载：用时、SHA256 一致；下载中途断网再恢复后可重试，失败不留残缺文件。
5. 用 tag 工作流产物在一台干净电脑上安装并完成 1–3。
记录到 `docs/release-checklist.md`，附时间、版本、哈希。
