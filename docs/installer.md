# Windows 安装器说明

`LANChat-Setup-x64.exe` 是 LAN Chat 的标准 Windows 安装包。房主和成员安装后使用同一个 `LANChat.exe`；程序会根据所选模式创建本地聊天室或加入局域网聊天室。

## 安装内容

- 默认安装到当前用户的 `%LocalAppData%\Programs\LAN Chat`，无需管理员权限。
- 创建开始菜单快捷方式；安装时可选择创建桌面快捷方式。
- 在 Windows“已安装的应用”中登记 `LAN Chat` 卸载项。
- 包含现代 GUI、Qt WebEngine 运行时、OpenSSL 运行时和本地 Go Server。
- 不包含任何初始证书、私钥、数据库、聊天记录、源码或编译器。

首次作为房主启动时，程序会把新生成的房主身份和数据库写入：

```text
%LocalAppData%\DUSK1NG\LAN Chat\host\
```

安装目录可安全升级或删除；房主数据不会存放在其中。

## 卸载行为

在 Windows“已安装的应用”中卸载 `LAN Chat`，或运行安装目录里的 `unins000.exe`。卸载器只删除程序文件、快捷方式和卸载注册项；不会静默删除 `%LocalAppData%\DUSK1NG\LAN Chat\host\` 中的 TLS 私钥、数据库或聊天记录。

如需完全清除本机房主数据，请在卸载后确认不再需要它们，再手动删除上述 `host` 目录。

## 维护者构建

先生成并验证统一运行包，再编译安装器：

```powershell
cd C:\path\to\chat_X
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-unified-release.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-installer.ps1 -Version 1.1.1
```

安装器使用 Inno Setup 6。若尚未安装，可先执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-installer.ps1 -ValidateOnly
```
