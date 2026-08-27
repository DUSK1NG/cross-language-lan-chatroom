# Flutter Release Resize Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 生成可供用户验证窗口缩放流畅度的 Flutter Windows Release 程序。

**Architecture:** 不修改应用源码或协议；复用已经通过测试的 MSVC Release Core DLL，通过 Flutter Windows Release 构建打包匹配的 Qt 与 OpenSSL 运行库。构建后用自动启动检查验证程序和动态库加载，再由用户手动验证缩放体验。

**Tech Stack:** Flutter Windows、Dart、CMake、MSVC、Qt 6.10.3、OpenSSL 3.6.3、CTest。

## Global Constraints

- Flutter UI、原生 Core、通信协议和全部现有功能保持不变。
- Release 程序不得显示 DEBUG 标记。
- 运行库必须从 Release 程序目录加载。
- 不读取、复制、输出或散列证书、私钥、数据库、聊天记录或日志内容。

---

### Task 1: 验证并生成 Release 程序

**Files:**
- Verify: `flutter_client/test/*.dart`
- Consume: `out/flutter-core-vs2022/Release/lan_chat_core.dll`
- Generate: `flutter_client/build/windows/x64/runner/Release/lan_chat_flutter.exe`

**Interfaces:**
- Consumes: `LAN_CHAT_CORE_DLL`、`LAN_CHAT_QT_PREFIX`、`LAN_CHAT_OPENSSL_ROOT` 构建环境变量。
- Produces: 可独立启动的 Windows Release 运行目录。

- [ ] **Step 1: 验证 Core 测试**

Run:
```powershell
ctest --test-dir out/flutter-core-vs2022 -C Release -R lan-chat-core --output-on-failure
```
Expected: `100% tests passed, 0 tests failed out of 2`。

- [ ] **Step 2: 验证 Flutter 测试与静态分析**

Run:
```powershell
flutter test
flutter analyze
```
Expected: 43 项通过、1 项受控原生测试跳过；`No issues found!`。

- [ ] **Step 3: 构建 Windows Release**

Run:
```powershell
$env:LAN_CHAT_CORE_DLL = 'C:/Users/Q1573/Desktop/MY_project/lan-chat/out/flutter-core-vs2022/Release/lan_chat_core.dll'
$env:LAN_CHAT_QT_PREFIX = 'C:/Users/Q1573/Desktop/MY_project/lan-chat/.tools/qt/6.10.3/msvc2022_64'
$env:LAN_CHAT_OPENSSL_ROOT = 'C:/Users/Q1573/Desktop/MY_project/lan-chat/.tools/vcpkg/installed/x64-windows'
flutter build windows --release
```
Expected: `Built build\windows\x64\runner\Release\lan_chat_flutter.exe`。

- [ ] **Step 4: 验证 Release 运行时**

Run: 隐藏启动 Release EXE 3 秒，确认进程未退出；检查 `lan_chat_core.dll`、两个 OpenSSL DLL 和五个 Qt DLL 均从同一 Release 目录加载，然后仅停止该测试进程。

Expected: `已加载目标运行库: 8/8`，程序不提前退出。

- [ ] **Step 5: 检查仓库状态并交付**

Run:
```powershell
git diff --check
git status --short --branch
```
Expected: 除用户已有的 `LANChat-Launcher.exe` 外无新增源码修改；向用户提供 Release EXE 的绝对路径，并请用户连续拖动窗口边缘验证流畅度。
