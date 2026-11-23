# GitHub 发布清单

Windows 用户从安装器或统一运行包启动 LAN Chat。

## 发布前验证

```powershell
cd C:\path\to\chat_X

git status --short
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-desktop.ps1

cargo test --workspace --locked

powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-source-launcher.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-unified-release.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-installer.ps1
```

要求：工作区只包含预期改动，Rust 测试和源码启动器 dry-run 检查通过。

## 发布资产

### 1. Windows 安装器

文件名：`LANChat-Setup-x64.exe`

使用 `scripts\package-installer.ps1` 从已经验证的统一运行目录生成。安装器按当前用户安装，不请求管理员权限；默认创建桌面与开始菜单快捷方式，并在 Windows“已安装的应用”中注册卸载项。卸载程序与快捷方式时保留房主运行后生成的私钥和数据库，避免静默删除数据。

### 2. 统一运行包（便携）

文件名：`LANChat-Desktop-Windows-x64.zip`

使用 `scripts\package-unified-release.ps1` 生成。局域网包包含桌面客户端、房主服务端和应用图标。

每台电脑都运行同一个 `LANChat.exe`。创建本地聊天室时，程序启动房主服务并按需生成 TLS 证书、私钥和数据库。成员加入时使用房主的公开证书。

产物为 `release\LANChat-Desktop-Windows-x64\` 与 `release\LANChat-Desktop-Windows-x64.zip`。脚本检查包内容并验证桌面客户端启动。

### 3. 源码启动器包（开发者可选）

文件名：`LANChat-Source-Launcher-windows-x64.zip`

使用 `scripts\package-source-launcher.ps1` 生成。它只复制 Git 跟踪的源码，并在压缩前后验证不含证书、私钥、数据库、构建输出和本机依赖缓存；随后把预编译的静态 `LANChat-Launcher.exe` 放在包根目录。

用户双击该 EXE 后，由 `scripts/bootstrap-github.ps1` 使用已有的构建依赖编译并启动桌面客户端。

## 严禁进入 GitHub 或 Release 资产的内容

- `server-lan.key`、任何 `.key` / `.pem`
- `chat.db`、历史消息、日志与访问令牌
- `out/`、`.tools/`、`node_modules/` 和本机构建缓存
- 房主生成的证书目录

可以公开：源码、构建脚本、文档、测试、CI 配置，以及说明如何让房主单独生成/分发公开证书的文字。

## 推送

完成本地验证且 CI 通过后，由仓库维护者执行：

```powershell
git push origin master
```

发布前确认 GitHub Actions 的 Rust 服务端、前端、Windows 桌面与源码启动器检查均通过。在 Windows 发布机验证 WebView2 启动和统一包。
