# P9 性能等级校准

该流程只使用诊断构建。发布构建不包含 `PerformanceSampler` 或右上角性能面板。

## 启动

```powershell
$env:PATH = "C:\Qt\6.11.2\msvc2022_64\bin;C:\Users\jking1\AppData\Local\Temp\lan-chat-vcpkg\vcpkg_installed\vcpkg\pkgs\openssl_x64-windows\bin;$env:PATH"
$env:QSG_INFO = "1"
$env:QT_LOGGING_RULES = "qt.scenegraph.general=true;qt.rhi.*=true"
& .\client-cpp\gui\build-webengine-msvc-perf-p9b\lan-chat-gui.exe
```

## 采样方法

在空闲、打开设置页、切换聊天页、滚动消息和发送消息五种状态下各观察至少 30 秒，记录右上角的 FPS、P95、P99、Max，以及设置页中的 Automatic 生效等级和策略原因。分别在 60 Hz、120/144 Hz 和软件渲染环境重复。

## 判定标准

- 单个短时尖峰不应改变等级。
- 连续高 P95 应在确认窗口后降级。
- 连续恢复后才应升级，不能在两个等级之间来回抖动。
- 软件渲染、无硬件加速和 60 Hz 以下刷新率属于立即生效的硬限制。

调整阈值前，应先在多个场景和多个窗口重复得到相同趋势；不要根据显卡型号写死分支。
