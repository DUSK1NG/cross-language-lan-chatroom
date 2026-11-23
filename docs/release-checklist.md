# 发布基线与恢复验收清单

更新：2026-11-22。此清单用于区分“代码回归”和“运行了旧包或过期构建产物”。每次进入新功能阶段前、制作发布包前各执行一次。

## 0. 工作树与产物身份

在项目根目录执行：

```powershell
git status --short
git rev-parse --short HEAD
Get-Item .\out\desktop\LANChat.exe, .\release\LANChat-Desktop-Windows-x64.zip, .\release\LANChat-Setup-x64.exe
```

- 先记录输出；未跟踪的个人素材、`tmp\`、构建缓存和发布产物都不得顺手加入提交。
- 验收前确认所启动的 `LANChat.exe` 来自本次构建目录或解压目录。

## 1. 自动化回归

```powershell
# 在当前项目根目录执行

# 前端：以本次运行输出为准
pnpm.cmd --dir .\frontend exec vitest run

# Windows 桌面构建与 Rust 测试
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-desktop.ps1
cargo test --workspace --locked

# 统一 ZIP 与安装器烟雾测试
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-unified-release.ps1 -SkipBuild
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-installer.ps1 -SmokeTest
```

前端和 Rust 测试、运行包启动检查及安装器编译均应通过。

若预检提示缺少构建依赖，在源代码工作目录执行一次：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\bootstrap-github.ps1
```

按 README 补齐缺失的构建工具，再执行本节命令。

## 2. 发布包安全检查

```powershell
$zip = Join-Path $PWD 'release\LANChat-Desktop-Windows-x64.zip'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
$forbidden = $archive.Entries | Where-Object {
  $path = $_.FullName.Replace('\', '/')
  $path -notin @('LANChat.exe', 'LANChat.ico', 'README.md', 'lan-chat-server.exe')
}
$archive.Dispose()
if ($forbidden) { $forbidden.FullName; throw '发布 ZIP 含不应交付的文件。' }
```

局域网包包含 `LANChat.exe`、`lan-chat-server.exe`、图标和说明。在线包提供桌面客户端、图标和说明。

## 3. 人工双机验收

1. 房主从全新解压 ZIP 或全新安装目录启动 `LANChat.exe`，选择“创建本地聊天室”。确认 `%LocalAppData%\DUSK1NG\LAN Chat\host\` 才生成 `server-lan.crt`、`server-lan.key` 和 `chat.db`。
2. 成员在另一台电脑启动同一包，使用“附近聊天室”连接，或在 UDP 被阻止时使用房主 IPv4、端口和公开证书的手动回退。
3. 房主确认待审批成员后，双方收发中文、表情、私信、引用、撤回与频道消息；成员未获批准前不得进入聊天室。
4. 连续发送多条消息：发送方应跟随最新消息；正在查看历史的接收方应获得新消息计数而不被强制拉到底部。
5. 关闭成员进程并重新打开，确认可以再次加入；停止再启动房主服务端时，记录 GUI 的实际状态和是否需要手动重连。

断开后恢复网络，检查客户端重连状态和聊天记录。

## 4. 安装器验收与卸载

1. 使用 `LANChat-Setup-x64.exe` 在非开发目录安装，检查开始菜单和桌面快捷方式图标。
2. 从快捷方式启动，完成一次本地主机和一次成员连接。
3. 从“已安装的应用”卸载，确认快捷方式和卸载项被移除；用户的 `%LocalAppData%\DUSK1NG\LAN Chat\host\` 身份数据按卸载提示决定是否保留。当前已知 Windows“设置”卸载可能遗留少量安装目录文件，记录即可，不作为 v1.2.0 发布阻塞项。

## 5. 记录结果

### v1.3.0

- 记录时间：2026-10-23 07:46:53 +08:00。
- 构建源码：master，提交 cccf3ab。
- GitHub CI：Go server、React frontend、C++ core (headless)、C++ GUI (Windows)、Windows source launcher 全部成功。
- 本机验证：前端 lint、189 项测试、Go 测试和 CTest 19/19 通过；MLS++ 已启用，CTest 无跳过。
- 包验证：统一包内容检查、成员及房主启动烟雾测试和 ZIP 安全检查通过；安装器前置烟雾测试及 Inno Setup 编译通过，AppVersion 为 1.3.0。
- 更新提示：跨进程重启后的已读状态测试通过；打包启动检查确认使用隔离的持久化界面配置目录。
- 干净机器及双机人工验收：未完成。

| 文件 | SHA256 |
| --- | --- |
| LANChat-Windows-x64.zip | 08894d7b39ef1ff5b436502d28013ed5c5673843000f626e45f5f5e3e1ace069 |
| LANChat-Setup-x64.exe | e43b2ae2e0e018f0fc13055acb6e8e3d9770a4c7fb64df52064b2fef376069d6 |
| out/modern-msvc-x64/lan-chat-gui.exe | 32cb9bd8368897de8275cc7a117807ad8833bf03744eb9f355a824a9dad0d34a |
