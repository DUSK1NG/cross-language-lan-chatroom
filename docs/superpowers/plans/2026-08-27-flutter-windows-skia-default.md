# Flutter Windows Default Skia Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 LAN Chat Flutter Windows 独立程序默认使用 Skia，消除 Impeller OpenGLESSDF 导致的窗口缩放卡顿，同时保留全部现有功能。

**Architecture:** 在干净功能分支中仅通过 Flutter Windows runner 的公开 `DartProject` API 关闭项目默认 Impeller。现有诊断分支只用于复用 FrameTiming 探针验证性能阈值；生产分支不携带探针、轮询开关或其他诊断代码。

**Tech Stack:** Flutter 3.47.1、Dart 3.13.1、Flutter Windows C++ runner、MSVC、CMake、Qt 6.10.3、OpenSSL、CTest。

## Global Constraints

- 生产源码只修改 `flutter_client/windows/runner/main.cpp`。
- 不修改 Flutter Widget UI、原生 Core、协议、Qt 旧客户端、React 前端或 Go 服务端。
- 不合并 A/B 轮询开关和 FrameTiming 探针等诊断代码。
- 默认 Profile 的 Raster P95 必须低于 16.67ms，且启动日志不得显示 Impeller 后端。
- Windows Release 必须成功构建，8 个目标运行库全部从程序目录加载。
- 不读取、复制、输出或散列证书、私钥、数据库、聊天记录或日志内容。

---

### Task 1: 在干净分支设置默认 Skia

**Files:**
- Modify: `flutter_client/windows/runner/main.cpp:19`

**Interfaces:**
- Consumes: `flutter::DartProject::set_impeller_switch(flutter::ImpellerSwitch)`。
- Produces: Windows runner 在没有显式 Flutter 工具覆盖参数时使用 Skia；`--enable-impeller=true` 仍可覆盖项目默认值。

- [ ] **Step 1: 创建隔离工作树**

Run:
```powershell
git check-ignore -q .worktrees
git worktree add ".worktrees/flutter-windows-skia-default" -b "codex/flutter-windows-skia-default" master
git -C ".worktrees/flutter-windows-skia-default" status --short --branch
```
Expected: `.worktrees` 被 Git 忽略；新分支基于包含规格与计划的 `master`，工作树干净。

- [ ] **Step 2: 确认性能 RED 基线**

读取已保存的同机、同应用、同自动缩放流程结果：默认 Impeller Raster P95 为 `109–113ms`，Total P95 为 `118–209ms`；使用 `--no-enable-impeller` 时 Raster P95 为 `4.8–5.7ms`。

Expected: 默认渲染路径超过 `16.67ms` 阈值，修复目标明确；无需再次运行已确认的慢速基线。

- [ ] **Step 3: 实现最小修复**

把 `flutter::DartProject project(L"data");` 后的代码改为：

```cpp
  flutter::DartProject project(L"data");
  project.set_impeller_switch(flutter::ImpellerSwitch::Disabled);

  std::vector<std::string> command_line_arguments =
      GetCommandLineArguments();
```

Expected: 只新增一个 API 调用，不改命令行参数、窗口创建或消息循环。

- [ ] **Step 4: 检查并提交单文件修复**

Run:
```powershell
git -C ".worktrees/flutter-windows-skia-default" diff --check
git -C ".worktrees/flutter-windows-skia-default" diff --name-only
git -C ".worktrees/flutter-windows-skia-default" add -- "flutter_client/windows/runner/main.cpp"
git -C ".worktrees/flutter-windows-skia-default" commit -m "fix: default Flutter Windows to Skia"
```
Expected: `diff --name-only` 只输出 `flutter_client/windows/runner/main.cpp`；提交成功。

### Task 2: 使用现有探针确认性能 GREEN

**Files:**
- Consume: `.worktrees/flutter-resize-polling-ab/flutter_client/lib/diagnostics/frame_timing_probe.dart`
- Temporarily update diagnostic branch: `.worktrees/flutter-resize-polling-ab/flutter_client/windows/runner/main.cpp`
- Generate: `.worktrees/flutter-resize-polling-ab/flutter_client/build/windows/x64/runner/Profile/`

**Interfaces:**
- Consumes: Task 1 的单文件修复提交、`LAN_CHAT_FRAME_TIMING_PROBE=true`、Windows Profile 窗口句柄。
- Produces: 默认启动路径的 Build/Raster/Total FrameTiming JSON；诊断分支不会合并到生产分支。

- [ ] **Step 1: 将同一修复提交应用到诊断分支**

Run:
```powershell
$fixCommit = git -C ".worktrees/flutter-windows-skia-default" rev-parse HEAD
git -C ".worktrees/flutter-resize-polling-ab" cherry-pick $fixCommit
git -C ".worktrees/flutter-resize-polling-ab" show --stat --oneline HEAD
```
Expected: cherry-pick 只修改 `flutter_client/windows/runner/main.cpp`；既有 FrameTiming 探针仍仅存在于诊断分支。

- [ ] **Step 2: 不带关闭 Impeller 参数启动 Profile**

Run from `.worktrees/flutter-resize-polling-ab`:
```powershell
$env:LAN_CHAT_CORE_DLL = 'C:/Users/Q1573/Desktop/MY_project/lan-chat/out/flutter-core-vs2022/Release/lan_chat_core.dll'
$env:LAN_CHAT_QT_PREFIX = 'C:/Users/Q1573/Desktop/MY_project/lan-chat/.tools/qt/6.10.3/msvc2022_64'
$env:LAN_CHAT_OPENSSL_ROOT = 'C:/Users/Q1573/Desktop/MY_project/lan-chat/.tools/vcpkg/installed/x64-windows'
Push-Location flutter_client
C:/Users/Q1573/Tools/flutter/bin/flutter.bat run -d windows --profile --no-devtools --dart-define=LAN_CHAT_FRAME_TIMING_PROBE=true
```
Expected: Profile 程序启动；命令中不存在 `--no-enable-impeller`，控制台不出现 `Using the Impeller rendering backend`。

- [ ] **Step 3: 重复自动缩放流程**

在另一 PowerShell 中运行：
```powershell
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ResizeWindowNative {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int width, int height, bool repaint);
  public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
'@
$profilePath = '\flutter-resize-polling-ab\flutter_client\build\windows\x64\runner\Profile\'
$process = Get-Process lan_chat_flutter | Where-Object { $_.Path -like "*$profilePath*" } | Select-Object -First 1
if (-not $process -or $process.MainWindowHandle -eq 0) { throw '未找到本次 Profile 窗口' }
$original = New-Object ResizeWindowNative+RECT
[ResizeWindowNative]::GetWindowRect($process.MainWindowHandle, [ref]$original) | Out-Null
$watch = [Diagnostics.Stopwatch]::StartNew()
for ($index = 0; $index -lt 120; $index++) {
  if ($index % 2 -eq 0) { $width = 900; $height = 620 } else { $width = 1380; $height = 860 }
  [ResizeWindowNative]::MoveWindow($process.MainWindowHandle, $original.Left, $original.Top, $width, $height, $true) | Out-Null
  Start-Sleep -Milliseconds 40
}
[ResizeWindowNative]::MoveWindow($process.MainWindowHandle, $original.Left, $original.Top, $original.Right - $original.Left, $original.Bottom - $original.Top, $true) | Out-Null
$watch.Stop()
"Resize elapsed: $($watch.Elapsed.TotalSeconds)s"
```
Expected: 完成 120 次尺寸变化并恢复原窗口；Flutter 控制台至少输出一个完整的 `LAN_CHAT_FRAME_TIMING` 60 帧批次。

- [ ] **Step 4: 停止并判定 GREEN**

在 Flutter run 终端输入 `q`，提取 `LAN_CHAT_FRAME_TIMING` JSON。

Expected: 每个完整批次的 Raster P95 均低于 `16.67ms`；启动日志无 Impeller；自动缩放耗时明显接近已测 Skia 路径而非 20.5 秒的 Impeller 路径。若任一条件失败，停止 Release 构建并继续诊断，不宣称修复完成。

### Task 3: 回归、Release 与运行库验证

**Files:**
- Test: `.worktrees/flutter-windows-skia-default/flutter_client/test/*.dart`
- Consume: `out/flutter-core-vs2022/Release/lan_chat_core.dll`
- Generate: `.worktrees/flutter-windows-skia-default/flutter_client/build/windows/x64/runner/Release/lan_chat_flutter.exe`

**Interfaces:**
- Consumes: Task 1 的干净修复分支与现有 Release Core、Qt、OpenSSL。
- Produces: 通过测试且可独立启动的 Windows Release 目录。

- [ ] **Step 1: 运行 Flutter 与 Core 回归测试**

Run:
```powershell
Push-Location ".worktrees/flutter-windows-skia-default/flutter_client"
C:/Users/Q1573/Tools/flutter/bin/flutter.bat test
C:/Users/Q1573/Tools/flutter/bin/flutter.bat analyze
Pop-Location
C:/Users/Q1573/Documents/Codex/2026-08-18/wei/tools/cmake/bin/ctest.exe --test-dir out/flutter-core-vs2022 -C Release -R lan-chat-core --output-on-failure
```
Expected: Flutter 全量测试通过且只有既有的 1 项受控原生测试跳过；`No issues found!`；Core `2/2` 通过。

- [ ] **Step 2: 构建干净分支 Release**

Run:
```powershell
$env:LAN_CHAT_CORE_DLL = 'C:/Users/Q1573/Desktop/MY_project/lan-chat/out/flutter-core-vs2022/Release/lan_chat_core.dll'
$env:LAN_CHAT_QT_PREFIX = 'C:/Users/Q1573/Desktop/MY_project/lan-chat/.tools/qt/6.10.3/msvc2022_64'
$env:LAN_CHAT_OPENSSL_ROOT = 'C:/Users/Q1573/Desktop/MY_project/lan-chat/.tools/vcpkg/installed/x64-windows'
Push-Location ".worktrees/flutter-windows-skia-default/flutter_client"
C:/Users/Q1573/Tools/flutter/bin/flutter.bat build windows --release
Pop-Location
```
Expected: 输出 `Built build\windows\x64\runner\Release\lan_chat_flutter.exe`，且没有 DEBUG 标记。

- [ ] **Step 3: 验证独立启动与 8 个运行库来源**

Run:
```powershell
$releaseDir = (Resolve-Path ".worktrees/flutter-windows-skia-default/flutter_client/build/windows/x64/runner/Release").Path
$exe = Join-Path $releaseDir 'lan_chat_flutter.exe'
$targets = @(
  'lan_chat_core.dll',
  'libssl-3-x64.dll',
  'libcrypto-3-x64.dll',
  'Qt6Core.dll',
  'Qt6Gui.dll',
  'Qt6Network.dll',
  'Qt6Quick.dll',
  'Qt6Qml.dll'
)
$process = Start-Process -FilePath $exe -WorkingDirectory $releaseDir -WindowStyle Hidden -PassThru
try {
  Start-Sleep -Seconds 3
  if ($process.HasExited) { throw "Release 提前退出，退出码 $($process.ExitCode)" }
  $loaded = @{}
  foreach ($module in $process.Modules) {
    if ($targets -contains $module.ModuleName) {
      $loaded[$module.ModuleName] = $module.FileName
    }
  }
  $missing = $targets | Where-Object { -not $loaded.ContainsKey($_) }
  $outside = $loaded.GetEnumerator() | Where-Object {
    [IO.Path]::GetDirectoryName($_.Value) -ne $releaseDir
  }
  "已加载目标运行库: $($loaded.Count)/$($targets.Count)"
  if ($missing) { throw "缺少目标运行库: $($missing -join ', ')" }
  if ($outside) { throw "存在目录外运行库: $($outside.Value -join ', ')" }
} finally {
  if (-not $process.HasExited) { Stop-Process -Id $process.Id }
}
```

Expected: `已加载目标运行库: 8/8`，目录外目标运行库为 `0`。

- [ ] **Step 4: 最终差异与分支检查**

Run:
```powershell
git -C ".worktrees/flutter-windows-skia-default" diff --check HEAD^ HEAD
git -C ".worktrees/flutter-windows-skia-default" diff --name-only master...HEAD
git -C ".worktrees/flutter-windows-skia-default" status --short --branch
git status --short --branch
```
Expected: 功能分支相对 `master` 只修改 `flutter_client/windows/runner/main.cpp`；功能工作树干净；主工作树只有用户已有的 `LANChat-Launcher.exe` 未跟踪。

- [ ] **Step 5: 交付用户手动确认**

提供以下绝对路径并请用户连续拖动四条窗口边缘与四个角：

```text
C:/Users/Q1573/Desktop/MY_project/lan-chat/.worktrees/flutter-windows-skia-default/flutter_client/build/windows/x64/runner/Release/lan_chat_flutter.exe
```

Expected: 用户确认缩放流畅且连接、主机搜索、CA 自动发现、聊天、用户管理、封禁、踢出和服务端管理入口仍然可用；确认后再进入分支合并与主目录最终构建。
