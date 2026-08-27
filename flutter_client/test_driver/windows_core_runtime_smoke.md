# Windows Core Runtime Smoke Test

在一个全新的 PowerShell 进程中执行；先关闭所有已有的 Flutter Windows
窗口，且不要复用其 PATH 或 DLL 加载状态。

```powershell
$env:LAN_CHAT_CORE_DLL = 'C:\Users\Q1573\Desktop\MY_project\lan-chat\.worktrees\flutter-ui-prototype\out\modern-msvc-x64\lan_chat_core.dll'
$env:LAN_CHAT_QT_PREFIX = 'C:\Users\Q1573\Desktop\MY_project\lan-chat\.tools\qt\6.10.3\msvc2022_64'
$env:LAN_CHAT_OPENSSL_ROOT = 'C:\Users\Q1573\Desktop\MY_project\lan-chat\.tools\vcpkg\installed\x64-windows'
Set-Location 'C:\Users\Q1573\Desktop\MY_project\lan-chat\.worktrees\flutter-ui-prototype\flutter_client'
flutter build windows --debug
```

确认 `build\windows\x64\runner\Debug` 中包含 `lan_chat_core.dll`、
`Qt6Core.dll`、`Qt6Gui.dll`、`Qt6Network.dll`、`Qt6Quick.dll`、
`Qt6OpenGL.dll`、`Qt6Qml.dll`、`Qt6QmlModels.dll`、`Qt6QmlMeta.dll`、
`Qt6QmlWorkerScript.dll`、`libssl-3-x64.dll` 与 `libcrypto-3-x64.dll`，
然后在另一个新 PowerShell 进程中运行：

```powershell
$env:LAN_CHAT_CORE_DLL = 'C:\Users\Q1573\Desktop\MY_project\lan-chat\.worktrees\flutter-ui-prototype\out\modern-msvc-x64\lan_chat_core.dll'
$env:LAN_CHAT_CORE_RUNTIME_DIR = 'C:\Users\Q1573\Desktop\MY_project\lan-chat\.worktrees\flutter-ui-prototype\flutter_client\build\windows\x64\runner\Debug'
Set-Location 'C:\Users\Q1573\Desktop\MY_project\lan-chat\.worktrees\flutter-ui-prototype\flutter_client'
flutter test test\lan_chat_core_test.dart
Start-Process .\build\windows\x64\runner\Debug\lan_chat_flutter.exe
```

`flutter test` 的 native 分支只在其宿主未预加载不同 ABI 的 Qt 时执行；测试宿主
已加载冲突 Qt 时应保留 mock 覆盖并记录原因。若宿主的 DLL 搜索路径已受控，可额外
设置 `LAN_CHAT_CORE_NATIVE_TEST=1` 执行 `openForTest()` 分支。新启动的 Debug runner 必须加载其
目录中 Qt 6.10.3 与 OpenSSL 3.6.3 的 DLL，显示连接页，且不能出现 `error code: 127`。
