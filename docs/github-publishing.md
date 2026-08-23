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
```

要求：工作区只包含预期改动；现代构建与全部 CTest 通过；源码启动器 dry-run 测试通过。

## 发布资产

### 1. 源码启动器包

文件名：`LANChat-Source-Launcher-windows-x64.zip`

应包含完整源码、`scripts/`、`tools/bootstrap/` 和预编译的静态 `LANChat-Launcher.exe`。用户双击该 EXE 后，由 `scripts/bootstrap-github.ps1` 检查依赖、征求首次安装确认、安装 Node.js/pnpm 等工具链、增量构建并启动现代 GUI。

### 2. 成员测试包

文件名：`LANChat-member-modern-x64.zip`

只包含已经部署的现代 GUI 和运行时文件。它不包含编译器、Node、Go SDK、源码、服务器、数据库、私钥或自动编译入口。成员只需要房主提供的 IPv4、端口和公开 `.crt`。

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

发布前确认 GitHub Actions 的 Go、前端与源码启动器检查均通过。完整 Qt WebEngine 运行时和成员包的烟雾验证在 Windows 发布机执行，并记录在 Release 说明中。
