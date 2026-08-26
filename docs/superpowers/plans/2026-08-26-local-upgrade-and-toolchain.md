# LAN Chat Local Upgrade and Toolchain Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 补齐 Windows 现代构建工具链，用当前仓库 `df3fdb7` 构建、测试和打包 LAN Chat v1.2.0，并安全覆盖升级现有 v1.1.7。

**Architecture:** 复用仓库现有 PowerShell 安装、构建和打包脚本，不修改产品源码。先完成环境与数据基线，再安装仓库本地 Qt/vcpkg 依赖和系统级 MSVC/Inno Setup；所有测试与包安全检查通过后才运行同 AppId 的每用户安装器。

**Tech Stack:** PowerShell 7/Windows PowerShell、winget、Visual Studio 2022 Build Tools、Qt 6 MSVC（优先 6.11.2；官方 Windows SDK 不可用时显式使用最新可下载兼容版）、OpenSSL 3/vcpkg、Node.js/pnpm、Go、CMake/Ninja、Inno Setup 6。

## Global Constraints

- 源码固定为当前仓库 `HEAD` `df3fdb7`；本计划和规格文档提交不改变产品源码基线。
- 不执行 `git reset --hard`、`git clean`，不删除未跟踪的 `LANChat-Launcher.exe`。
- 不读取或输出 `server-lan.key`、`chat.db`、聊天记录及日志内容。
- 房主数据目录 `%LocalAppData%\DUSK1NG\LAN Chat\host` 不参与卸载、打包或覆盖安装。
- 任一构建、测试、包安全检查失败时停止覆盖安装，现有 v1.1.7 保持可用。
- 工具链安装允许使用 winget、pip、Git 和 vcpkg 下载数 GB 依赖，并允许正常 UAC 授权；不得绕过系统权限控制。

---

### Task 1: 建立升级与数据保护基线

**Files:**
- Inspect: `C:\Users\Q1573\Desktop\MY_project\lan-chat\.git`
- Inspect metadata only: `%LocalAppData%\DUSK1NG\LAN Chat\host`
- Inspect: `%LocalAppData%\Programs\LAN Chat`

**Interfaces:**
- Consumes: 当前 Git 工作树、Windows 卸载项和已安装文件。
- Produces: 可与安装后状态比较的提交号、版本号、房主数据文件元数据。

- [ ] **Step 1: 确认源码基线和工作树边界**

Run:

```powershell
git -c safe.directory='C:/Users/Q1573/Desktop/MY_project/lan-chat' log -1 --oneline
git -c safe.directory='C:/Users/Q1573/Desktop/MY_project/lan-chat' status --short
```

Expected: HEAD 包含 `df3fdb7` 作为产品构建基线之后的首个设计提交；工作树除 `LANChat-Launcher.exe` 外没有产品源码改动。若计划文档已提交，允许 HEAD 位于后续仅文档提交。

- [ ] **Step 2: 记录现有安装版本**

Run:

```powershell
$uninstallRoots = @(
  'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
  'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
  'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
)
Get-ItemProperty $uninstallRoots -ErrorAction SilentlyContinue |
  Where-Object DisplayName -eq 'LAN Chat' |
  Select-Object DisplayName, DisplayVersion, InstallLocation
```

Expected: `DisplayVersion` 为 `1.1.7`，安装目录为当前用户 LocalAppData 下的 `Programs\LAN Chat`。

- [ ] **Step 3: 只记录房主数据文件元数据**

Run:

```powershell
$hostData = Join-Path $env:LOCALAPPDATA 'DUSK1NG\LAN Chat\host'
Get-Item -LiteralPath $hostData -ErrorAction SilentlyContinue | Select-Object FullName, LastWriteTime
Get-ChildItem -LiteralPath $hostData -File -Recurse -ErrorAction SilentlyContinue |
  Where-Object Name -in @('server-lan.crt','server-lan.key','chat.db') |
  Select-Object FullName, Length, LastWriteTime
```

Expected: 只输出路径、长度和时间，不读取文件内容。保存该命令输出用于 Task 5 对比。

### Task 2: 安装完整构建与打包工具链

**Files:**
- Execute: `scripts/install-modern-toolchain.ps1`
- Read: `tools/bootstrap/lan-chat.vsconfig`
- Generated/ignored: `.tools/qt/6.11.2/msvc2022_64`
- Generated/ignored: `.tools/vcpkg/installed/x64-windows`
- External state: Visual Studio Build Tools、Python、pnpm、Inno Setup 6

**Interfaces:**
- Consumes: winget、npm、pip、Git 和仓库 `.vsconfig`。
- Produces: `cl.exe`/MSVC、Qt CMake 配置、vcpkg OpenSSL、`ISCC.exe`，供 Tasks 3-4 使用。

- [ ] **Step 1: 确认 winget 可用**

Run:

```powershell
winget --version
winget source list
```

Expected: 两条命令退出码为 0，`winget` 源可用。

- [ ] **Step 2: 执行仓库工具链安装器**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\install-modern-toolchain.ps1
```

Expected: 最终输出 `Dependency installation completed.`；允许 winget 报告已有 Node/Go/Git 包无需重装。

- [ ] **Step 3: 安装 Inno Setup 6**

Run:

```powershell
winget install --exact --id JRSoftware.InnoSetup --source winget --accept-source-agreements --accept-package-agreements
```

Expected: 安装成功或报告已安装；记录 `ISCC.exe` 的实际绝对路径。winget 按用户安装时允许位于 `%LocalAppData%\Programs\Inno Setup 6\ISCC.exe`。

- [ ] **Step 4: 验证仓库要求的工具链**

Run:

```powershell
$qtPrefix = (Resolve-Path .\.tools\qt\6.10.3\msvc2022_64).Path
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -CheckOnly -QtPrefix $qtPrefix
```

Expected: Node.js、pnpm、Go、CMake、Ninja、MSVC、实际安装的 Qt 6 MSVC SDK 和 OpenSSL 均输出实际路径，命令退出码为 0。2026-08-26 执行时 Qt 6.11.2 Windows SDK 元数据在官方源返回 404，因此透明使用可下载的 6.10.3，并在所有构建命令中显式传入该路径。

### Task 3: 完整构建和自动化验证

**Files:**
- Execute: `scripts/build-modern.ps1`
- Execute: `scripts/tests/start-gui-wait-regression.ps1`
- Generated/ignored: `frontend/dist`
- Generated/ignored: `server-go/chat-server.exe`
- Generated/ignored: `out/modern-msvc-x64`

**Interfaces:**
- Consumes: Task 2 的完整工具链。
- Produces: 已通过前端、Go、Qt/C++ 验证的 Release 构建，供 Task 4 打包。

- [ ] **Step 1: 运行前端测试**

Run:

```powershell
pnpm.cmd --dir .\frontend test -- --run
```

Expected: `13 passed`、`79 passed`，退出码为 0。

- [ ] **Step 2: 运行 Go 完整验证**

Run:

```powershell
Push-Location .\server-go
try {
  go test -count=1 ./...
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  go test -race -count=1 ./...
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  go vet ./...
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally { Pop-Location }
```

Expected: 两次 Go 测试均报告 `ok cross-language-lan-chat/server-go`，`go vet` 退出码为 0。

- [ ] **Step 3: 构建前端、Go、Qt/C++ 并运行 CTest**

Run:

```powershell
$qtPrefix = (Resolve-Path .\.tools\qt\6.10.3\msvc2022_64).Path
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Test -QtPrefix $qtPrefix
```

Expected: Vite 生产构建、Go Server、Qt WebEngine 客户端构建成功，CTest `100% tests passed, 0 tests failed out of 15`。

- [ ] **Step 4: 验证源码启动器参数转发**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\tests\start-gui-wait-regression.ps1
```

Expected: 输出 `start-gui wait forwarding regression passed.`。

### Task 4: 生成并验证 v1.2.0 安装器

**Files:**
- Execute: `scripts/package-unified-release.ps1`
- Execute: `scripts/package-installer.ps1`
- Generated/ignored: `release/LANChat-Windows-x64`
- Generated/ignored: `release/LANChat-Windows-x64.zip`
- Generated/ignored: `release/LANChat-Setup-x64.exe`

**Interfaces:**
- Consumes: Task 3 的 `out/modern-msvc-x64` 和 `server-go/chat-server.exe`。
- Produces: 已通过禁止文件检查和烟雾验证的 v1.2.0 安装器。

- [ ] **Step 1: 生成统一运行包**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-unified-release.ps1 -SkipBuild
```

Expected: 复用 Task 3 已验证的构建，生成 `release\LANChat-Windows-x64` 和 `release\LANChat-Windows-x64.zip`，安全验证报告禁止文件数量为 0。

- [ ] **Step 2: 编译安装器并运行烟雾检查**

Run:

```powershell
$iscc = Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-installer.ps1 -Version 1.2.0 -SmokeTest -InnoCompiler $iscc
```

Expected: 运行包验证成功并生成 `release\LANChat-Setup-x64.exe`。

- [ ] **Step 3: 记录安装器哈希**

Run:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath .\release\LANChat-Setup-x64.exe
```

Expected: 输出一个 64 位十六进制 SHA-256；只用于本次安装结果记录。

### Task 5: 覆盖安装并验证数据保留

**Files:**
- Execute: `release/LANChat-Setup-x64.exe`
- Modify external installation: `%LocalAppData%\Programs\LAN Chat`
- Preserve: `%LocalAppData%\DUSK1NG\LAN Chat\host`

**Interfaces:**
- Consumes: Task 4 已验证安装器和 Task 1 基线。
- Produces: 已安装的 LAN Chat 1.2.0、有效快捷方式和未改变的房主数据元数据。

- [ ] **Step 1: 停止正在使用安装目录的 LAN Chat 进程**

Run:

```powershell
Get-Process LANChat,lan-chat-gui,chat-server,QtWebEngineProcess -ErrorAction SilentlyContinue |
  Stop-Process -Force
```

Expected: 仅停止 LAN Chat 及其子进程；再次执行 `Get-Process` 不返回这些名称。

- [ ] **Step 2: 执行无人值守覆盖安装**

Run:

```powershell
$installer = (Resolve-Path .\release\LANChat-Setup-x64.exe).Path
$process = Start-Process -FilePath $installer -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/TASKS=desktopicon' -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "LAN Chat installer failed with exit code $($process.ExitCode)." }
```

Expected: 安装器退出码为 0，不启动应用，不要求重启。

- [ ] **Step 3: 验证卸载项和运行文件**

Run:

```powershell
$entry = Get-ItemProperty 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*' -ErrorAction SilentlyContinue |
  Where-Object DisplayName -eq 'LAN Chat' | Select-Object -First 1
$entry | Select-Object DisplayName, DisplayVersion, InstallLocation
$required = @(
  'LANChat.exe','lan-chat-gui.exe','server-go\chat-server.exe','Qt6Core.dll',
  'Qt6WebChannel.dll','Qt6WebEngineCore.dll','Qt6WebEngineWidgets.dll',
  'QtWebEngineProcess.exe','openssl\libssl-3-x64.dll','openssl\libcrypto-3-x64.dll'
)
$required | ForEach-Object {
  $path = Join-Path $entry.InstallLocation $_
  [PSCustomObject]@{ Path = $path; Exists = Test-Path -LiteralPath $path -PathType Leaf }
}
```

Expected: `DisplayVersion` 为 `1.2.0`，所有 `Exists` 均为 `True`。

- [ ] **Step 4: 核对房主数据元数据未被安装器改动**

Run:

```powershell
$hostData = Join-Path $env:LOCALAPPDATA 'DUSK1NG\LAN Chat\host'
Get-ChildItem -LiteralPath $hostData -File -Recurse -ErrorAction SilentlyContinue |
  Where-Object Name -in @('server-lan.crt','server-lan.key','chat.db') |
  Select-Object FullName, Length, LastWriteTime
```

Expected: 路径、长度和修改时间与 Task 1 相同；不读取或显示文件内容。

- [ ] **Step 5: 执行安装后启动烟雾验证**

Run:

```powershell
$installRoot = Join-Path $env:LOCALAPPDATA 'Programs\LAN Chat'
$launcher = Join-Path $installRoot 'LANChat.exe'
$process = Start-Process -FilePath $launcher -WorkingDirectory $installRoot -PassThru
Start-Sleep -Seconds 8
if ($process.HasExited) { throw "Installed LAN Chat exited early with code $($process.ExitCode)." }
Stop-Process -Id $process.Id -Force
Get-Process lan-chat-gui,chat-server,QtWebEngineProcess -ErrorAction SilentlyContinue | Stop-Process -Force
```

Expected: 应用至少稳定运行 8 秒；验证后只关闭本次烟雾启动产生的 LAN Chat 进程。

- [ ] **Step 6: 最终检查仓库未出现意外源码修改**

Run:

```powershell
git -c safe.directory='C:/Users/Q1573/Desktop/MY_project/lan-chat' status --short
```

Expected: 只允许原有 `LANChat-Launcher.exe` 和计划/规格文档的预期状态；不得出现产品源码修改、私钥、数据库、日志或 release/out 产物被跟踪。

