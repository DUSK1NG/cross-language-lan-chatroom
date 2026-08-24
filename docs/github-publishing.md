# GitHub 发布清单

仓库的唯一正式用户路径是现代 React + Qt WebEngine 客户端。发布前不要把旧 CLI、旧 QML 构建目录或本机调试输出作为用户入口。

## 发布前验证

```powershell
cd C:\path\to\chat_X

git status --short
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Test

cmake -S .\tools\bootstrap -B .\out\launcher-release -G Ninja
cmake --build .\out\launcher-release --target LANChat-Launcher --parallel 4
ctest --test-dir .\out\launcher-release --output-on-failure

powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-source-launcher.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-unified-release.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-installer.ps1
```

要求：工作区只包含预期改动；现代构建与全部 CTest 通过；源码启动器 dry-run 测试通过。

## 发布资产

### 1. Windows 安装器

文件名：`LANChat-Setup-x64.exe`

使用 `scripts\package-installer.ps1` 从已经验证的统一运行目录生成。安装器按当前用户安装，不请求管理员权限；默认创建桌面与开始菜单快捷方式，并在 Windows“已安装的应用”中注册卸载项。卸载程序与快捷方式时保留房主运行后生成的私钥和数据库，避免静默删除数据。

### 2. 统一运行包（便携）

文件名：`LANChat-Windows-x64.zip`

使用 `scripts\package-unified-release.ps1` 生成。它包含已经部署的现代 GUI、Qt WebEngine、OpenSSL、MSVC 运行时和 `server-go\chat-server.exe`，不包含源码、Node.js、Go SDK、编译器、自动编译入口、初始证书、私钥或数据库。

每台电脑都运行同一个 `LANChat.exe`：它在检测到本地服务端时，先在该房主电脑生成 TLS 证书、私钥和数据库，再启动现代 GUI；成员机则直接进入 GUI。房主只向成员分发公开的 `server-lan.crt`，绝不分发私钥。

产物固定为 `release\LANChat-Windows-x64\` 与 `release\LANChat-Windows-x64.zip`。脚本会部署 Qt WebEngine、OpenSSL 与 MSVC 运行时，加入已构建的 Go Server，执行烟雾检查，并拒绝把证书、私钥、数据库、源码或构建工具打进包中。

### 3. 源码启动器包（开发者可选）

文件名：`LANChat-Source-Launcher-windows-x64.zip`

使用 `scripts\package-source-launcher.ps1` 生成。它只复制 Git 跟踪的源码，并在压缩前后验证不含证书、私钥、数据库、构建输出和本机依赖缓存；随后把预编译的静态 `LANChat-Launcher.exe` 放在包根目录。

用户双击该 EXE 后，由 `scripts/bootstrap-github.ps1` 检查依赖、征求首次安装确认、安装 Node.js/pnpm 等工具链、增量构建并启动现代 GUI。

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

发布前确认 GitHub Actions 的 Go、前端与源码启动器检查均通过。完整 Qt WebEngine 运行时和统一运行包的烟雾验证在 Windows 发布机执行，并记录在 Release 说明中。
