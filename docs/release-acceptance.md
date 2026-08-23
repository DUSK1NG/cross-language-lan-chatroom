# 发布前验收

本项目的正式用户入口是 React + Qt WebEngine 客户端。发布前必须同时完成以下三类验收，不能用旧 CLI、旧 QML 构建目录或性能诊断构建替代。

## 已完成的手动联调

- 房主在本机创建局域网聊天室，Go TLS 服务端成功启动并自动连接。
- 第二台 Windows PC 使用 `LANChat-Windows-x64.zip` 独立启动，填写房主真实局域网 IPv4、端口 `8888` 和 `server-lan.crt` 后成功加入。
- 两台电脑之间的消息收发验证完成。

## 每次发布前执行

```powershell
cd C:\path\to\chat_X

git status --short
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Test
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-unified-release.ps1
```

验收要求：工作树只包含预期文档或源码改动；现代构建的 CTest 通过；统一运行 ZIP 的安全检查与烟雾检查通过。

## 交付边界

- 统一运行包：仅包含已部署的现代客户端、运行时和 `server-go\chat-server.exe`。它不含 Node.js、Go、编译器、源码、初始私钥、证书、数据库或自动编译入口。
- GitHub 源码启动器包：仅作为开发者可选工具，包含源码与 `LANChat-Launcher.exe`；首次启动会明确询问后才安装构建依赖并自动编译。
- 房主仅向成员传递 IPv4、端口和公开的 `server-lan.crt`。`server-lan.key`、`chat.db`、聊天记录和任何访问令牌均不得进入 GitHub 或任何发布包。
