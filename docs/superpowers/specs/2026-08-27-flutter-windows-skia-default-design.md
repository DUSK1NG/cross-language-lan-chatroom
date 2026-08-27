# Flutter Windows 默认 Skia 渲染设计

## 根因

Flutter 3.47.1 在 Windows 上默认启用 Impeller OpenGLESSDF。LAN Chat 自动缩放采集显示，Impeller 的 Raster P95 为 109–113ms，Total P95 为 118–209ms；同一程序关闭 Impeller 后，Raster P95 降至 4.8–5.7ms，Total P95 降至 5.6–8.1ms。Build P95 始终低于 0.8ms，因此 UI 布局和 Core 轮询不是主要瓶颈。

## 方案

仅修改 Flutter Windows runner。在创建 `flutter::DartProject` 后调用 Flutter 官方公开 API：

```cpp
project.set_impeller_switch(flutter::ImpellerSwitch::Disabled);
```

这使独立启动的 Windows 程序默认使用 Skia。Flutter 工具通过环境传入的显式 `--enable-impeller=true` 开关仍会在引擎初始化时覆盖项目默认值，保留开发和后续复测 Impeller 的能力。

## 范围

- 修改 `flutter_client/windows/runner/main.cpp`。
- 不修改 Flutter Widget UI、原生 Core、协议、Qt 旧客户端、React 前端或 Go 服务端。
- 不合并 A/B 轮询开关和 FrameTiming 探针等诊断代码。
- Windows 以外平台不受影响。

## 验证

- 失败基线：已记录默认 Impeller Raster P95 109–113ms。
- 修复后使用不带 `--no-enable-impeller` 的 Profile 构建重复同一自动缩放流程，确认启动日志不再显示 Impeller，并且 Raster P95 低于 16.67ms。
- Flutter 全量测试、静态分析和 Core 测试继续通过。
- Windows Release 构建成功，独立启动时 8 个目标运行库均从程序目录加载。
- 用户手动连续拖动 Release 窗口边缘确认缩放体验。

## 回退

删除单行 `set_impeller_switch` 调用即可恢复 Flutter 平台默认渲染器，无数据迁移或协议回退。
