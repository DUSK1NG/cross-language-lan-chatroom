# Windows 安装器说明

`LANChat-Setup-x64.exe` 是统一运行包的安装形式。它适用于房主和成员；两者安装后运行的是同一个 `lan-chat-gui.exe`。

## 安装内容

- 默认安装到当前用户的 `%LocalAppData%\Programs\LAN Chat`，不需要管理员权限。
- 创建开始菜单“LAN Chat”快捷方式；默认勾选创建桌面快捷方式。
- 在 Windows“设置 → 应用 → 已安装的应用”中登记 `LAN Chat` 卸载项。
- 包含现代 GUI、Qt WebEngine 运行时和本地 Go Server；不包含初始证书、私钥、数据库、源码或编译器。

## 卸载行为

从 Windows“已安装的应用”选择 `LAN Chat` 后点击卸载，或运行安装目录中的 `unins000.exe`。

卸载器会移除程序文件、快捷方式和 Windows 卸载项。为避免静默删除房主的 TLS 私钥、聊天数据库和历史记录，它会保留运行后才产生的 `server-go\certs\` 与 `server-go\chat.db`。如需彻底清除本地房主数据，请在卸载完成后确认不再需要数据，再手动删除残留的 `server-go` 目录。

## 维护者构建

先生成并验证统一运行包，再编译安装器：

```powershell
cd C:\path\to\chat_X
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-unified-release.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-installer.ps1 -Version 1.1.0
```

安装器使用 Inno Setup 6。尚未安装时，先安装 Inno Setup 6，或将 `ISCC.exe` 的完整路径传给 `-InnoCompiler`。不安装编译器也可先执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-installer.ps1 -ValidateOnly
```
