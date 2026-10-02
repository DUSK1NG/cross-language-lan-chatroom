# 发布基线与恢复验收清单

更新：2026-09-09。此清单用于区分“代码回归”和“运行了旧包或过期构建产物”。每次进入新功能阶段前、制作发布包前各执行一次。

## 0. 工作树与产物身份

在项目根目录执行：

```powershell
git status --short
git rev-parse --short HEAD
Get-FileHash .\out\modern-msvc-x64\lan-chat-gui.exe -Algorithm SHA256
Get-FileHash .\release\LANChat-Windows-x64.zip -Algorithm SHA256
Get-FileHash .\release\LANChat-Setup-x64.exe -Algorithm SHA256
```

- 先记录输出；未跟踪的个人素材、`tmp\`、构建缓存和发布产物都不得顺手加入提交。
- 运行包内的 `lan-chat-gui.exe` 必须与 `out\modern-msvc-x64\lan-chat-gui.exe` 为同一次构建。若哈希不同，先重新打包，不能把运行现象归因于源码。

## 1. 自动化回归

```powershell
# 在当前项目根目录执行

# 前端：以本次运行输出为准
pnpm.cmd --dir .\frontend exec vitest run

# 工具链预检；若报告缺少 Node、Qt 或 OpenSSL，先从源码包运行 bootstrap，
# 不要把依赖缺失误判为产品构建失败。
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -CheckOnly

# Go、C++、Qt/WebEngine 集成测试
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Test

# 统一 ZIP 与安装器烟雾测试
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-unified-release.ps1 -SkipBuild
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-installer.ps1 -Version 1.2.0 -SmokeTest
```

通过标准：前端测试和 CTest 全部通过；统一包、安装器烟雾检查通过；允许出现 `WrapVulkanHeaders` 缺失提示，但不得出现测试失败或运行时 DLL 缺失。

若预检提示缺少构建依赖，在源代码工作目录执行一次：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\bootstrap-github.ps1
```

然后重新打开 PowerShell，并从本节第一条命令开始执行。当前机器本次验证使用的 OpenSSL 前缀是临时 vcpkg 目录；它只用于本机构建，不属于发布包。

## 2. 发布包安全检查

```powershell
$zip = Join-Path $PWD 'release\LANChat-Windows-x64.zip'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
$forbidden = $archive.Entries | Where-Object {
  $_.FullName -match '\.(key|pem|crt|db)$' -or
  $_.FullName -match '(^|/)(frontend|server-go|client-cpp)/' -or
  $_.FullName -match '(^|/)(node_modules|\.tools)/'
}
$archive.Dispose()
if ($forbidden) { $forbidden.FullName; throw '发布 ZIP 含不应交付的文件。' }
```

统一包必须包含 `LANChat.exe`、`lan-chat-gui.exe`、运行时 DLL、`server-go\chat-server.exe` 和应用图标；不得包含私钥、证书、数据库、聊天记录、源码或工具链。

## 3. 人工双机验收

1. 房主从全新解压 ZIP 或全新安装目录启动 `LANChat.exe`，选择“创建本地聊天室”。确认 `%LocalAppData%\DUSK1NG\LAN Chat\host\` 才生成 `server-lan.crt`、`server-lan.key` 和 `chat.db`。
2. 成员在另一台电脑启动同一包，使用“附近聊天室”连接，或在 UDP 被阻止时使用房主 IPv4、端口和公开证书的手动回退。
3. 房主确认待审批成员后，双方收发中文、表情、私信、引用、撤回与频道消息；成员未获批准前不得进入聊天室。
4. 连续发送多条消息：发送方应跟随最新消息；正在查看历史的接收方应获得新消息计数而不被强制拉到底部。
5. 关闭成员进程并重新打开，确认可以再次加入；停止再启动房主服务端时，记录 GUI 的实际状态和是否需要手动重连。

当前基线说明：CLI 已有退避重连；WebEngine GUI 仅在本机服务端启动窗口内重试，断线后的通用会话恢复属于后续 Phase 1，不能在本阶段声称已完成。

## 4. 安装器验收与卸载

1. 使用 `LANChat-Setup-x64.exe` 在非开发目录安装，检查开始菜单和桌面快捷方式图标。
2. 从快捷方式启动，完成一次本地主机和一次成员连接。
3. 从“已安装的应用”卸载，确认快捷方式和卸载项被移除；用户的 `%LocalAppData%\DUSK1NG\LAN Chat\host\` 身份数据按卸载提示决定是否保留。当前已知 Windows“设置”卸载可能遗留少量安装目录文件，记录即可，不作为 v1.2.0 发布阻塞项。

## 5. 记录结果
