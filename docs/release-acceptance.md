# 发布前验收

在 Windows 上验收 WebView2 桌面客户端、统一运行包和安装器。

## 每次发布前执行

```powershell
cd C:\path\to\chat_X

git status --short
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-desktop.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-unified-release.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-installer.ps1 -ValidateOnly
```

验收要求：工作树只包含预期文档或源码改动；桌面构建通过；统一运行 ZIP 的安全检查与烟雾检查通过。

安装器可用时还应手工确认：安装完成后桌面和开始菜单快捷方式都可启动现代 GUI；Windows“已安装的应用”出现 `LAN Chat`；卸载后程序与快捷方式被移除，而房主生成的证书和数据库不会被静默删除。

## 交付边界

- 局域网运行包包含桌面客户端、`lan-chat-server.exe`、图标和使用说明。在线运行包包含桌面客户端、图标和使用说明。
- GitHub 源码启动器包：仅作为开发者可选工具，包含源码与 `LANChat-Launcher.exe`；使用前需按 README 准备构建依赖。
- 自动发现只传递房主的公开证书及指纹；若自动发现不可用，房主才向成员传递 IPv4、端口和公开的 `server-lan.crt`。`server-lan.key`、`chat.db`、聊天记录和任何访问令牌均不得进入 GitHub 或任何发布包。

## 1.4.0 局域网验收记录

2026-11-23，单台 Windows，S0 提交 `aa04d78`，后续功能使用本地候选源码。

| 场景 | 结果 |
| --- | --- |
| S0 干净 CI | [四项检查通过](https://github.com/DUSK1NG/cross-language-lan-chatroom/actions/runs/45168616334) |
| 本机编译与回归 | 工作区测试和 Clippy、前端 188 项、版本校验 4 项、TLS 11 项通过。LAN ZIP 启动检查和 Inno Setup 编译通过 |
| 心跳与历史搜索 | 应用层心跳超时、重新审批、旧连接兼容、中文短词与长词、分页、撤回后搜索、FTS 回退及修复均通过。以 1.3 建立的 10,000 条测试消息迁移后行数一致，迁移及启动约 0.11 秒，库从 3.35 MB 增至 4.57 MB |
| 房间权限与兼容 | 旧密码房迁移、重启后的密码与权限、再次审批、同名重建及数据库写失败用例通过。实际 1.3 与 1.4 Windows 客户端在同机通过双向 TLS、发消息和历史搜索 |
| 签名更新与卸载 | 本机隔离环境以测试签名完成 1.4.0 到 1.4.1 更新和重启，保留房主证书、房间和历史，卸载后保留 SQLite。坏签名、错误版别、签名与清单版本不符均被拒绝。便携版显示 ZIP 入口 |
| 正常 Windows 安装与双机 | 待验收。当前会话写入卸载注册项及快捷方式时收到 Windows 拒绝访问。仍需验证旧正式安装覆盖、快捷方式、系统卸载，以及双机发现、审批、切网或休眠重连、附件与升级后数据 |
| 候选 CI、正式签名与上传 | 待验收。需要冻结候选源码并运行干净 CI，配置正式签名并完成本版适用的发布演练。上传范围为完成验收后的无数据 LAN 便携 ZIP |

## 1.4.0 候选验收范围

2026-11-24 在同一台 Windows 上验证 LAN、Online 和 Android 候选。LAN 双机按本轮要求跳过，Android 验收范围为本机 APK。

| 场景 | 本机结果 |
| --- | --- |
| 自动化 | Rust 工作区、Clippy、前端 lint/188 项/生产构建、版本检查 4 项和 LAN TLS 11 项通过 |
| LAN 附件并发 | Hub 已按职责拆分。验证工作队列满、并发聊天、退房后旧任务失效、分块校验及提交中断后重启续传。同机 100 MiB 附件提交回放中，1.3 与 1.4 的提交耗时分别约 251 ms、247 ms，同期另一连接的聊天耗时分别约 221 ms、12 ms |
| Online 迁移和群权限 | 用实际 1.3 服务端生成的旧 SQLite 验证只读统计、迁移重跑、校验后清理及恢复原服务端和数据。使用测试 CA 的隔离 Windows Online 客户端完成建群、公告、搜索和文件收发。自动化覆盖历史权限、旧客户端群附件文本展示和群主转让 |
| 本地 Windows 包 | 两版 ZIP 启动检查和 Inno Setup 编译通过。标准安装的注册项与快捷方式写入收到 Windows 拒绝访问，本轮采用隔离验收 |
| 隔离签名更新 | 两版使用独立测试签名完成 1.4.0 到 1.4.1 的 HTTPS 下载、退出、覆盖安装、自动启动和卸载，设置保留。拒绝另一版资产、另一版签名和版本不符。两版 RC 与稳定更新清单均成功生成 |
| Android APK | `dev.dusk1ng.lanchat` 0.4.0 / 16，ARM64，APK v2 签名校验通过，签名证书与原 0.3.3 本地调试 APK 一致 |
| 候选 CI 与交付 | `1c3ca6d` 的五项干净 CI 全部通过。GitHub 草稿资产仅含经过文件清单验证的无数据 LAN 便携 ZIP。[候选 CI](https://github.com/DUSK1NG/cross-language-lan-chatroom/actions/runs/45328095138) · [局域网便携版草稿](https://github.com/DUSK1NG/cross-language-lan-chatroom/releases/tag/untagged-6e6a970c9066964acb15) |

## Android 真机补充验收

2026-11-24 在小米 14 Pro（Android 16、ARM64）验证 `1c3ca6d` 的 0.4.0 调试候选。

| 场景 | 结果 |
| --- | --- |
| 原位升级 | 同一应用 ID 和签名的 0.3.3-rust（15）覆盖升级至 0.4.0（16）。用户数据目录与通知授权保留，ADB 冷启动成功 |
| 隔离服务 | 真机与 Windows 协议测试端通过 TLS 登录；私信、群消息和群图片双向收发通过。手机显示缩略图与全屏图片 |
| 群文件 | 接收与发送均通过。手机收到两分块文件后核对暂存内容，手动选择测试文件发送后由 Windows 端下载；两次均与源文件逐字节一致 |
| 群历史 | 重新打开群聊后选「加载更早的消息」，可翻到较早的测试消息 |
| 群管理 | 手机端邀请好友、任免管理员、转让群主和退群均通过，测试服务确认成员已移除 |
| 会话恢复 | 进程重启后测试账号和后台消息可恢复，测试服务重启后可自动重连。2026-11-25 用户确认 Wi-Fi 与移动网络实切后自动重连并可继续收发 |
| 通知 | 私信与群聊通知链接均打开目标会话。切换隔离包通知权限后账号可恢复。隔离 Firebase 项目的 FCM HTTP v1 仅校验请求通过。2026-11-25 在小米 14 Pro 验证真实 FCM，后台私信与锁屏群消息均送达，点击两条通知均打开对应会话。隔离测试包与 USB 转发已清理 |

11 月 24 日的验收中，ADB 崩溃日志里候选包和隔离包均为 0 条。当日的临时测试包、测试文件和 USB 转发已清理，原应用 ID 下保留 0.4.0。

## 线上服务端 1.4.0

2026-11-25 已切换线上 `lan-chat-online` 至 `1.4.0`。Linux 锁定构建、服务端单元测试、心跳测试和隔离网络中的 LAN 集成测试通过。实际旧库副本迁移后保留原有记录，2 个文件及 2 张图片逐字节一致，隔离 TLS 私信、群聊及旧版备份恢复均通过。

切换后从 Windows 验证线上 TLS 和登录响应。线上 SQLite `user_version` 为 3，完整性检查通过。切换前的二进制、数据库和 AstrBot 配置保存在服务器 `/var/backups/lan-chat-online/v1.4.0-H8K06wDp/`。AstrBot 服务已停止，后续按 [手动接入步骤](release-runbook.md#astrbot-手动接入) 启动。
