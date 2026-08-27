# Flutter Default Desktop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Flutter Windows 客户端构建为默认发布程序 `out/flutter-windows-release/LANChat-Launcher.exe`，同时保留 Qt/React 回退客户端。

**Architecture:** 合并现有 Flutter 原型及其 `lan_chat_core.dll` C ABI。新增一个 PowerShell 发布脚本，先复用现有 Qt/OpenSSL 构建生成原生核心，再执行 Flutter Windows Release 构建，最后将 Flutter bundle、FFI 核心、Qt 和 OpenSSL 运行时复制到自包含发布目录。

**Tech Stack:** Flutter 3.47.1 / Dart 3.13.1、Flutter Windows runner、dart:ffi、C++20、Qt 6.10.3、OpenSSL 3、PowerShell、CMake、CTest。

## Global Constraints

- 只支持 Windows x64；Flutter 是默认 UI，现有 Qt/React 只作为回退，不删除。
- 发布入口固定为 `out/flutter-windows-release/LANChat-Launcher.exe`；它必须与 Flutter 的 `data/`、`flutter_windows.dll`、`lan_chat_core.dll` 和依赖 DLL 位于同一发布目录。
- 运行时不得依赖系统 `PATH` 中的 Qt；必须使用项目 `.tools/qt/6.10.3/msvc2022_64` 与 `.tools/vcpkg/installed/x64-windows`。
- 不读取、复制或打包 `%LocalAppData%\DUSK1NG\LAN Chat\host` 下的私钥、证书、数据库、聊天记录或日志。
- 保留 `scripts/build-modern.ps1` 的 Qt/React 回退构建语义。
- 不修改用户未跟踪的 `LANChat-Launcher.exe`。

---

### Task 1: 合并并验证 Flutter 原型与 FFI 核心

**Files:**
- Add: `flutter_client/**`（来自 `codex/flutter-ui-prototype`）
- Modify: `client-cpp/gui/CMakeLists.txt`
- Add: `client-cpp/gui/src/flutter_core/lan_chat_core.cpp`
- Add: `client-cpp/gui/src/flutter_core/lan_chat_core.h`
- Add: `client-cpp/gui/tests/lan_chat_core_tests.cpp`

**Interfaces:**
- Consumes: 分支 `codex/flutter-ui-prototype` 的已提交 Flutter Windows 工程与 C ABI。
- Produces: `lan_chat_core.dll`、`lan-chat-core-tests`、`lan-chat-core-no-app-tests` 与 `flutter_client/`。

- [ ] **Step 1: 在隔离工作树合并原型提交**

Run:

```powershell
git merge --no-ff codex/flutter-ui-prototype -m "feat: integrate Flutter desktop prototype"
```

Expected: 合并 9 个 Flutter 原型提交；不包含 `.build/` 或 `flutter_client/.tool-state/` 未跟踪产物。

- [ ] **Step 2: 检查 FFI 构建入口**

Verify `client-cpp/gui/CMakeLists.txt` defines:

```cmake
add_library(lan-chat-core SHARED ${LAN_CHAT_CORE_SOURCES})
set_target_properties(lan-chat-core PROPERTIES OUTPUT_NAME lan_chat_core)
add_test(NAME lan-chat-core-tests COMMAND lan-chat-core-tests)
add_test(NAME lan-chat-core-no-app-tests COMMAND lan-chat-core-no-app-tests)
```

Verify Dart opens only the adjacent DLL:

```dart
DynamicLibrary.open(
  '${File(Platform.resolvedExecutable).parent.path}${Platform.pathSeparator}lan_chat_core.dll',
)
```

- [ ] **Step 3: 运行原型基线，确认合并产物可用**

Run:

```powershell
$qt = (Resolve-Path .\.tools\qt\6.10.3\msvc2022_64).Path
$openssl = (Resolve-Path .\.tools\vcpkg\installed\x64-windows).Path
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Test -QtPrefix $qt -OpenSslRoot $openssl
Set-Location .\flutter_client
flutter pub get
$env:LAN_CHAT_CORE_DLL = (Resolve-Path ..\out\modern-msvc-x64\lan_chat_core.dll).Path
flutter test
flutter analyze
```

Expected: CTest 包含两个 `lan-chat-core` 测试且全部通过；Flutter 测试和分析通过。

- [ ] **Step 4: 确认合并提交边界**

Run:

```powershell
git show --stat --oneline -1
git status --short
```

Expected: Step 1 已产生唯一的 `feat: integrate Flutter desktop prototype` 合并提交；没有把 `.build/`、`.tool-state/` 或用户未跟踪启动器加入 Git。

### Task 2: 以失败检查驱动 Flutter 发布脚本

**Files:**
- Create: `scripts/build-flutter.ps1`
- Create: `scripts/test-flutter-package.ps1`

**Interfaces:**
- Consumes: `scripts/build-modern.ps1`、`flutter_client/`、`out/modern-msvc-x64/lan_chat_core.dll`。
- Produces: `out/flutter-windows-release/LANChat-Launcher.exe` 与发布目录完整性验证命令。

- [ ] **Step 1: 写入发布目录失败检查**

Create `scripts/test-flutter-package.ps1`:

```powershell
[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageDirectory)

$required = @(
  'LANChat-Launcher.exe',
  'flutter_windows.dll',
  'lan_chat_core.dll',
  'Qt6Core.dll',
  'Qt6Network.dll',
  'libssl-3-x64.dll',
  'libcrypto-3-x64.dll',
  'data\icudtl.dat',
  'data\app.so',
  'data\flutter_assets\AssetManifest.bin'
)
$missing = $required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $PackageDirectory $_)) }
if ($missing) { throw "Flutter package is incomplete: $($missing -join ', ')" }
Write-Output "Flutter package is complete: $PackageDirectory"
```

- [ ] **Step 2: 运行检查确认当前发布目录不存在**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-flutter-package.ps1 -PackageDirectory .\out\flutter-windows-release
```

Expected: FAIL，提示发布目录或必要文件缺失。

- [ ] **Step 3: 实现确定性发布脚本**

Create `scripts/build-flutter.ps1` with `Package` and `CheckOnly` actions. The script must use this flow:

```powershell
$root = Split-Path -Parent $PSScriptRoot
$qt = (Resolve-Path (Join-Path $root '.tools\qt\6.10.3\msvc2022_64')).Path
$openssl = (Resolve-Path (Join-Path $root '.tools\vcpkg\installed\x64-windows')).Path
$flutterProject = Join-Path $root 'flutter_client'
$coreBuild = Join-Path $root 'out\modern-msvc-x64'
$package = Join-Path $root 'out\flutter-windows-release'

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'scripts\build-modern.ps1') `
  -Action Test -QtPrefix $qt -OpenSslRoot $openssl
$coreDll = (Resolve-Path (Join-Path $coreBuild 'lan_chat_core.dll')).Path
$env:LAN_CHAT_CORE_DLL = $coreDll
Push-Location $flutterProject
try {
  flutter pub get
  flutter build windows --release
} finally { Pop-Location }
```

Before copying, verify that `lan_chat_core.dll`, `flutter_client/build/windows/x64/runner/Release/lan_chat_flutter.exe`, `windeployqt.exe`, and both OpenSSL DLLs exist. Create `$package` only after resolving all source paths. Copy the entire Flutter release directory, rename only `lan_chat_flutter.exe` to `LANChat-Launcher.exe`, copy `lan_chat_core.dll` beside it, run:

```powershell
& (Join-Path $qt 'bin\windeployqt.exe') --dir $package $coreDll
Copy-Item (Join-Path $openssl 'bin\libssl-3-x64.dll') $package -Force
Copy-Item (Join-Path $openssl 'bin\libcrypto-3-x64.dll') $package -Force
```

Finally invoke `scripts/test-flutter-package.ps1`. Do not copy user data, source directories, `.build/`, `.tool-state/`, or `out/modern-msvc-x64` wholesale.

- [ ] **Step 4: 验证发布脚本**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-flutter.ps1 -Action Package
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-flutter-package.ps1 -PackageDirectory .\out\flutter-windows-release
```

Expected: 两个命令通过；发布目录中无对系统 PATH 的依赖。

- [ ] **Step 5: 提交发布流水线**

```powershell
git add scripts/build-flutter.ps1 scripts/test-flutter-package.ps1
git commit -m "feat: package Flutter Windows desktop client"
```

### Task 3: 将 Flutter 发布入口写入项目文档

**Files:**
- Modify: `README.md`
- Modify: `flutter_client/README.md`

**Interfaces:**
- Consumes: Task 2 的 `scripts/build-flutter.ps1` 与发布目录路径。
- Produces: 以 Flutter 为默认入口、Qt/React 为回退入口的明确操作文档。

- [ ] **Step 1: 写入失败的文档验证**

Run:

```powershell
rg -n "build-flutter\.ps1|flutter-windows-release|LANChat-Launcher\.exe" README.md flutter_client\README.md
```

Expected: FAIL，因为当前文档尚未定义默认 Flutter 发布入口。

- [ ] **Step 2: 更新默认与回退命令**

Add this exact default build and launch sequence to `README.md`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-flutter.ps1 -Action Package
Start-Process .\out\flutter-windows-release\LANChat-Launcher.exe
```

Add this exact fallback sequence in a separate “Qt/React 回退客户端” section:

```powershell
$qt = (Resolve-Path .\.tools\qt\6.10.3\msvc2022_64).Path
$openssl = (Resolve-Path .\.tools\vcpkg\installed\x64-windows).Path
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Launch -QtPrefix $qt -OpenSslRoot $openssl
```

In `flutter_client/README.md`, replace the “独立原型壳” wording with the default release flow, explain that `lan_chat_core.dll` is supplied by the package script, and state that users must keep all files in the release directory together.

- [ ] **Step 3: 验证文档可发现性**

Run:

```powershell
rg -n "build-flutter\.ps1|flutter-windows-release|LANChat-Launcher\.exe" README.md flutter_client\README.md
git diff --check
```

Expected: 两份文档都显示默认发布入口；无空白错误。

- [ ] **Step 4: 提交文档**

```powershell
git add README.md flutter_client/README.md
git commit -m "docs: make Flutter the default desktop entry"
```

### Task 4: 运行发布包冒烟测试与回归

**Files:**
- Inspect: `out/flutter-windows-release/**`
- Inspect: `out/modern-msvc-x64/**`
- Modify: none unless验证暴露真实缺陷。

**Interfaces:**
- Consumes: Flutter 发布目录、现有 Qt/React 构建脚本。
- Produces: 直接双击安全的 Flutter 发布目录和验证记录。

- [ ] **Step 1: 验证发布目录不加载系统 Qt**

Run:

```powershell
$package = (Resolve-Path .\out\flutter-windows-release).Path
$launcher = Join-Path $package 'LANChat-Launcher.exe'
$process = Start-Process -FilePath $launcher -WorkingDirectory $package -PassThru
Start-Sleep -Seconds 5
if ($process.HasExited) { throw "Flutter launcher exited with code $($process.ExitCode)" }
$process | Select-Object ProcessName, Id, MainWindowTitle
```

Expected: `LANChat-Launcher.exe` 或 Flutter runner 保持运行并显示 `LAN Chat` 窗口；不需要设置 Qt、OpenSSL 或 Flutter 的环境变量。

- [ ] **Step 2: 检查 Flutter 与 C++ 自动化回归**

Run:

```powershell
Set-Location .\flutter_client
flutter test
flutter analyze
Set-Location ..
$qt = (Resolve-Path .\.tools\qt\6.10.3\msvc2022_64).Path
$openssl = (Resolve-Path .\.tools\vcpkg\installed\x64-windows).Path
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Test -QtPrefix $qt -OpenSslRoot $openssl
```

Expected: Flutter 测试/分析通过；现有 Qt/React 回退客户端及全部 CTest 通过。

- [ ] **Step 3: 检查变更边界并提交验收记录（如有）**

Run:

```powershell
git diff --check
git status --short
```

Expected: 仅有计划内源码、脚本、文档和被忽略的构建产物；不修改未跟踪的 `LANChat-Launcher.exe`。纯验证不创建空提交。

## Self-Review

- Task 1 负责合并与 FFI 核心基线；Task 2 只负责确定性发布；Task 3 只负责默认入口文档；Task 4 只负责发布包与回退回归。
- 规格中的默认 Flutter UI、保留回退、固定发布入口、相邻 DLL、无 PATH 依赖、依赖失败提示、完整性检查与启动冒烟测试均有对应任务。
- 不包含删除 Qt/React、重写聊天协议或打包用户本地数据的步骤。
- 每个可实现任务都有先失败后通过的检查、准确文件范围和提交边界；脚本复制仅发生在 `out/` 生成目录。
