# LAN Chat 本机升级与开发工具链设计

**日期：** 2026-08-26
**状态：** 已实施并验证

## 目标

- 使用当前仓库 `HEAD`（`df3fdb7`）构建并安装 LAN Chat v1.2.0，覆盖现有 v1.1.7。
- 补齐本仓库完整现代构建、CTest、打包所需的 Windows 开发工具链。
- 保留 `%LocalAppData%\DUSK1NG\LAN Chat\host` 中的证书、私钥和数据库。
- 完成自动化验证、运行包安全检查和安装结果核对后再声明升级完成。

## 方案选择

考虑过三种路径：

1. 下载正式 v1.2.0 安装器，只升级应用，不补开发环境。
2. 使用当前仓库源码补齐完整工具链、构建、测试、打包并覆盖安装。
3. 只生成便携包，不修改现有安装。

选择方案 2。它同时满足“更新为仓库最新版”和“安装缺少的东西”，并能验证当前源码在本机真实可构建。

## 工具链安装

优先复用 `scripts/install-modern-toolchain.ps1`，安装或确认：

- Node.js LTS、pnpm 11.19.0、Go、Git、Python 3.12；
- Visual Studio 2022 Build Tools x64；
- Qt 6 MSVC SDK（6.11.2 优先；官方 Windows SDK 元数据不可用时使用已验证的 6.10.3），含 Qt WebEngine、Qt WebChannel 和 Qt Positioning；
- 仓库本地 `.tools/vcpkg` 与 `openssl:x64-windows`；
- Inno Setup 6，用于生成安装器。

已有工具允许安装器修复或升级；Qt 和 vcpkg 依赖保存在仓库忽略的 `.tools` 目录。安装完成后必须将实际 Qt 路径显式传给 `scripts/build-modern.ps1 -CheckOnly -QtPrefix <path>`，不能仅依据安装命令退出码判断成功。本机最终验证路径为 `.tools/qt/6.10.3/msvc2022_64`。

## 构建与升级流程

1. 记录现有安装版本、安装目录和房主数据目录，只读取，不复制私钥内容。
2. 关闭 LAN Chat、`chat-server.exe` 和 `QtWebEngineProcess.exe`，避免覆盖使用中的文件。
3. 安装工具链并通过依赖检查。
4. 执行完整现代构建与测试：前端、Go Server、Qt/C++ 及 CTest。
5. 使用 `scripts/package-unified-release.ps1` 生成统一运行包并执行敏感文件排除检查。
6. 使用 `scripts/package-installer.ps1 -Version 1.2.0 -SmokeTest` 生成安装器。
7. 静默或无人值守覆盖安装到现有每用户目录，不运行卸载清理，不删除 `%LocalAppData%` 房主数据。
8. 核对卸载项版本、安装文件、快捷方式和启动烟雾结果。

## 安全与失败处理

- 不执行 `git reset --hard`、`git clean`，不删除未跟踪的 `LANChat-Launcher.exe`。
- 不读取、输出、复制或打包 `server-lan.key`、`chat.db`、聊天记录和日志内容。
- 任一构建、测试、打包或安全检查失败时停止安装，保留现有 v1.1.7。
- 若安装器生成成功但覆盖安装失败，报告安装器路径和原安装状态，不手工删除安装目录。
- 工具链安装可能需要管理员授权、下载数 GB，并可能要求重新打开终端；按脚本结果继续，不绕过权限提示。

## 验收标准

- `scripts/build-modern.ps1 -CheckOnly -QtPrefix .tools/qt/6.10.3/msvc2022_64` 成功，并输出实际解析路径。
- 前端测试与生产构建成功。
- `go test ./...`、`go test -race ./...`、`go vet ./...` 成功。
- Qt/C++ 完整构建成功，CTest 全部通过。
- 统一运行包与安装器烟雾检查成功，禁止文件数量为 0。
- Windows 卸载项显示 LAN Chat 1.2.0，安装目录包含完整 Qt WebEngine、OpenSSL 和 Go Server 运行文件。
- 房主证书、私钥和数据库路径保持不变，升级后仍存在；不检查或披露其内容。
