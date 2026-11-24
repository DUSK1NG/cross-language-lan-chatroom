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
| LAN 附件并发 | Hub 已按职责拆分。验证工作队列满、并发聊天、退房后旧任务失效、分块校验及提交中断后重启续传 |
| Online 迁移和群权限 | SQLite 旧图片和文件演练通过只读统计、重跑、可校验清理及读取。群成员加入前的历史隔离、被移除与重新加入后的附件权限、旧客户端的群附件文本展示、群主转让用例通过 |
| 本地 Windows 包 | 两版 ZIP 启动检查和 Inno Setup 编译通过。标准安装的注册项与快捷方式写入仍收到 Windows 拒绝访问 |
| Android APK | `dev.dusk1ng.lanchat` 0.4.0 / 16，ARM64，APK v2 签名校验通过。原 0.3.3 本地调试 APK 与当前调试签名不同 |
| 正式签名发布 | 正式发布环境和密钥尚未配置。按已授权范围在独立分支运行候选 CI |
