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
