# WinUI 3 Phase 1 Core Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立可独立启动的非打包 WinUI 3 客户端壳，通过现有 C ABI 安全加载真实 `lan_chat_core.dll`，并显示初始 Core 状态或可操作的加载诊断。

**Architecture:** 新建 `winui_client` 解决方案，分为无 UI 依赖的 `LanChat.Core`、可测试的 `LanChat.Presentation` 和仅承载 XAML/Windows 生命周期的 `LanChat.WinUI`。`CoreRuntime` 在专用线程串行拥有 Core，通过事件把不可变状态交给 ViewModel，WinUI 使用 `DispatcherQueue` 应用更新。

**Tech Stack:** .NET SDK 10.0.400、C# 14、WinUI 3、Windows App SDK 2.4.0、WinApp CLI 0.6.1、MSTest、现有 C ABI Core、Qt 6.10.3、OpenSSL 3.6.3。官方模板生成器单次使用 `Microsoft.WindowsAppSDK.WinUI.CSharp.Templates 0.0.6-alpha`，不进入应用运行时依赖图。

## Global Constraints

- 新代码仅位于 `winui_client/`、根目录 `global.json` 和本阶段必要的构建文档；不修改 Flutter、Qt、React、Go 服务端、通信协议或数据库。
- 目标系统为 Windows 10 1809（build 17763）及以上和 Windows 11；构建目标为 x64。
- 应用必须非打包运行，设置 `WindowsPackageType=None` 和 `WindowsAppSDKSelfContained=true`；不得要求 MSIX 安装。
- 应用运行时的 Windows App SDK 固定为稳定版 `2.4.0`，.NET SDK 固定为 `10.0.400`，WinApp CLI 固定为 `0.6.1`；不得使用 preview 或 experimental 运行时包。仅允许官方脚手架生成器临时使用 `Microsoft.WindowsAppSDK.WinUI.CSharp.Templates 0.0.6-alpha`，生成后不得将其保留为应用依赖。
- C# 只调用现有六个 `lan_chat_core_*` C ABI 接口；同一 Core 句柄的所有调用必须在一个专用后台线程串行执行。
- UI 不读取、复制、输出、哈希或上传私钥、证书内容、数据库、聊天记录或日志内容。
- `lan_chat_core.dll` 或依赖加载失败必须显示实际路径和系统错误，不得静默返回虚假空状态。
- Flutter、Qt 和现有默认入口保持不变；本阶段不得合并 Flutter Skia 修复或 resize 诊断代码。
- 安装前检查 C 盘可用空间；低于 10GB 时停止并告知用户，不自动清理任何内容。

---

### Task 1: 固定工具链并创建解决方案骨架

**Files:**
- Create: `global.json`
- Create: `winui_client/LANChat.WinUI.slnx`
- Create: `winui_client/Directory.Build.props`
- Create: `winui_client/src/LanChat.Core/LanChat.Core.csproj`
- Create: `winui_client/src/LanChat.Presentation/LanChat.Presentation.csproj`
- Create: `winui_client/src/LanChat.WinUI/LanChat.WinUI.csproj`
- Create: `winui_client/tests/LanChat.Core.Tests/LanChat.Core.Tests.csproj`
- Create: `winui_client/tests/LanChat.Presentation.Tests/LanChat.Presentation.Tests.csproj`

**Interfaces:**
- Consumes: .NET SDK 10.0.400、WinApp CLI 0.6.1、Windows SDK 10.0.22621.0。
- Produces: 可还原、可构建、启用包锁定的四项目解决方案。

- [ ] **Step 1: 检查磁盘和现有工具链**

Run:
```powershell
$systemDrive = Get-PSDrive -Name C
$freeGb = [math]::Round($systemDrive.Free / 1GB, 2)
"C free: $freeGb GB"
if ($freeGb -lt 10) { throw 'C 盘可用空间低于 10GB；停止安装并等待用户决定是否清理。' }
dotnet --list-sdks
winapp --version
```
Expected: C 盘不少于 10GB；当前基线应显示没有 .NET SDK，WinApp CLI 可能尚未安装。

- [ ] **Step 2: 安装固定版本工具链**

Run:
```powershell
winget install --id Microsoft.DotNet.SDK.10 --exact --version 10.0.400 --accept-package-agreements --accept-source-agreements --silent
winget install --id Microsoft.WinAppCli --exact --version 0.6.1 --accept-package-agreements --accept-source-agreements --silent
```
Expected: 两项安装成功；如果 winget 报告已安装相同版本，视为成功。安装不会触发磁盘清理。

- [ ] **Step 3: 验证版本并固定 SDK**

Run:
```powershell
dotnet --version
winapp --version
dotnet new globaljson --sdk-version 10.0.400 --roll-forward latestPatch --force
```
Expected: 输出 `10.0.400` 和 `0.6.1`；根目录生成：

```json
{
  "sdk": {
    "version": "10.0.400",
    "rollForward": "latestPatch"
  }
}
```

- [ ] **Step 4: 使用官方模板创建应用并创建可测试项目**

Run:
```powershell
winapp new --template winui --name LanChat.WinUI --output winui_client/src/LanChat.WinUI --template-version 0.0.6-alpha --no-prompt --json
dotnet new classlib -n LanChat.Core -o winui_client/src/LanChat.Core --framework net10.0
dotnet new classlib -n LanChat.Presentation -o winui_client/src/LanChat.Presentation --framework net10.0
dotnet new mstest -n LanChat.Core.Tests -o winui_client/tests/LanChat.Core.Tests --framework net10.0
dotnet new mstest -n LanChat.Presentation.Tests -o winui_client/tests/LanChat.Presentation.Tests --framework net10.0
dotnet new sln -n LANChat.WinUI --format slnx -o winui_client
dotnet sln winui_client/LANChat.WinUI.slnx add winui_client/src/LanChat.Core/LanChat.Core.csproj winui_client/src/LanChat.Presentation/LanChat.Presentation.csproj winui_client/src/LanChat.WinUI/LanChat.WinUI.csproj winui_client/tests/LanChat.Core.Tests/LanChat.Core.Tests.csproj winui_client/tests/LanChat.Presentation.Tests/LanChat.Presentation.Tests.csproj
```
Expected: 官方 WinUI 空白模板位于 `src/LanChat.WinUI`，其余项目和 `.slnx` 均存在。

说明：`0.0.6-alpha` 是独立的官方模板包版本，仅在本命令中用作一次性生成器；应用项目的 `Microsoft.WindowsAppSDK` 运行时包仍必须固定为稳定 `2.4.0`，且不得引用该 alpha 模板包。

- [ ] **Step 5: 固定共同构建设置和项目依赖**

创建 `winui_client/Directory.Build.props`：

```xml
<Project>
  <PropertyGroup>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
    <TreatWarningsAsErrors>true</TreatWarningsAsErrors>
    <RestorePackagesWithLockFile>true</RestorePackagesWithLockFile>
    <Deterministic>true</Deterministic>
  </PropertyGroup>
</Project>
```

把 `LanChat.WinUI.csproj` 的主属性固定为：

```xml
<PropertyGroup>
  <OutputType>WinExe</OutputType>
  <TargetFramework>net10.0-windows10.0.19041.0</TargetFramework>
  <TargetPlatformMinVersion>10.0.17763.0</TargetPlatformMinVersion>
  <RuntimeIdentifier>win-x64</RuntimeIdentifier>
  <PlatformTarget>x64</PlatformTarget>
  <WindowsPackageType>None</WindowsPackageType>
  <WindowsAppSDKSelfContained>true</WindowsAppSDKSelfContained>
  <PublishSingleFile>false</PublishSingleFile>
  <UseWinUI>true</UseWinUI>
</PropertyGroup>
<ItemGroup>
  <PackageReference Include="Microsoft.WindowsAppSDK" Version="2.4.0" />
  <ProjectReference Include="..\LanChat.Core\LanChat.Core.csproj" />
  <ProjectReference Include="..\LanChat.Presentation\LanChat.Presentation.csproj" />
</ItemGroup>
```

给 `LanChat.Presentation` 引用 `LanChat.Core`，两个测试项目分别引用被测项目：

```powershell
dotnet add winui_client/src/LanChat.Presentation/LanChat.Presentation.csproj reference winui_client/src/LanChat.Core/LanChat.Core.csproj
dotnet add winui_client/tests/LanChat.Core.Tests/LanChat.Core.Tests.csproj reference winui_client/src/LanChat.Core/LanChat.Core.csproj
dotnet add winui_client/tests/LanChat.Presentation.Tests/LanChat.Presentation.Tests.csproj reference winui_client/src/LanChat.Presentation/LanChat.Presentation.csproj
```

- [ ] **Step 6: 还原、构建并提交骨架**

Run:
```powershell
dotnet restore winui_client/LANChat.WinUI.slnx --use-lock-file
dotnet build winui_client/LANChat.WinUI.slnx -c Debug --no-restore
git diff --check
git add global.json winui_client
git commit -m "build: scaffold WinUI client"
```
Expected: 解决方案零警告、零错误；`packages.lock.json` 被提交；不触及旧客户端。

### Task 2: 安全的 C ABI 边界

**Files:**
- Create: `winui_client/src/LanChat.Core/Interop/ILanChatCoreNative.cs`
- Create: `winui_client/src/LanChat.Core/Interop/PInvokeLanChatCoreNative.cs`
- Create: `winui_client/src/LanChat.Core/Interop/CoreLibraryResolver.cs`
- Create: `winui_client/src/LanChat.Core/Interop/LanChatCoreHandle.cs`
- Create: `winui_client/src/LanChat.Core/Properties/AssemblyInfo.cs`
- Create: `winui_client/src/LanChat.Core/LanChatCoreClient.cs`
- Create: `winui_client/src/LanChat.Core/ILanChatCore.cs`
- Create: `winui_client/tests/LanChat.Core.Tests/FakeLanChatCoreNative.cs`
- Create: `winui_client/tests/LanChat.Core.Tests/LanChatCoreClientTests.cs`

**Interfaces:**
- Consumes: 六个现有 C ABI 导出和 UTF-8 所有权约定。
- Produces: `ILanChatCore`：`ReadStateJson()`、`Dispatch(string)`、`DrainEvents()`、`Dispose()`。

- [ ] **Step 1: 写 Native 生命周期失败测试**

测试必须覆盖：创建返回空句柄时报错；状态字符串在成功解码后释放；事件读到空指针停止；`Dispose()` 两次只销毁一次；命令保持 UTF-8 中文。

```csharp
[TestMethod]
public void Dispose_releases_handle_once()
{
    var native = new FakeLanChatCoreNative { CreateResult = (nint)42 };
    var client = new LanChatCoreClient(native);

    client.Dispose();
    client.Dispose();

    Assert.AreEqual(1, native.DestroyCallCount);
    Assert.AreEqual((nint)42, native.DestroyedHandle);
}

[TestMethod]
public void ReadStateJson_decodes_utf8_and_frees_native_string()
{
    var native = new FakeLanChatCoreNative { CreateResult = (nint)42 };
    native.EnqueueState("{\"status\":\"未连接\"}");
    using var client = new LanChatCoreClient(native);

    Assert.AreEqual("{\"status\":\"未连接\"}", client.ReadStateJson());
    Assert.AreEqual(1, native.FreeStringCallCount);
}
```

- [ ] **Step 2: 运行测试并确认 RED**

Run:
```powershell
dotnet test winui_client/tests/LanChat.Core.Tests/LanChat.Core.Tests.csproj --filter "Dispose_releases_handle_once|ReadStateJson_decodes_utf8_and_frees_native_string"
```
Expected: 编译失败，提示 `LanChatCoreClient` 和 `FakeLanChatCoreNative` 不存在。

- [ ] **Step 3: 实现接口、P/Invoke 和 SafeHandle**

`ILanChatCoreNative` 精确接口：

```csharp
namespace LanChat.Core.Interop;

internal interface ILanChatCoreNative
{
    nint Create();
    void Destroy(nint handle);
    int DispatchJson(nint handle, string commandJson);
    nint CurrentStateJson(nint handle);
    nint TakeEventJson(nint handle);
    void FreeString(nint value);
}
```

`PInvokeLanChatCoreNative` 使用 `LibraryImport`，固定 UTF-8 和 Cdecl：

```csharp
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace LanChat.Core.Interop;

internal sealed partial class PInvokeLanChatCoreNative : ILanChatCoreNative
{
    private const string Library = "lan_chat_core.dll";

    [LibraryImport(Library, EntryPoint = "lan_chat_core_create")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial nint CreateNative();

    [LibraryImport(Library, EntryPoint = "lan_chat_core_destroy")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial void DestroyNative(nint handle);

    [LibraryImport(Library, EntryPoint = "lan_chat_core_dispatch_json", StringMarshalling = StringMarshalling.Utf8)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial int DispatchNative(nint handle, string commandJson);

    [LibraryImport(Library, EntryPoint = "lan_chat_core_current_state_json")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial nint CurrentStateNative(nint handle);

    [LibraryImport(Library, EntryPoint = "lan_chat_core_take_event_json")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial nint TakeEventNative(nint handle);

    [LibraryImport(Library, EntryPoint = "lan_chat_core_free_string")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial void FreeStringNative(nint value);

    public nint Create() => CreateNative();
    public void Destroy(nint handle) => DestroyNative(handle);
    public int DispatchJson(nint handle, string commandJson) => DispatchNative(handle, commandJson);
    public nint CurrentStateJson(nint handle) => CurrentStateNative(handle);
    public nint TakeEventJson(nint handle) => TakeEventNative(handle);
    public void FreeString(nint value) => FreeStringNative(value);
}
```

`LanChatCoreHandle` 保存 native 实例，并在 `ReleaseHandle()` 中只调用一次 `Destroy(handle)`。`LanChatCoreClient` 构造时创建句柄；所有返回字符串以 `Marshal.PtrToStringUTF8` 解码，并在 `finally` 中调用 `FreeString`；状态空指针抛出 `InvalidOperationException("原生核心未返回状态快照。")`。`Properties/AssemblyInfo.cs` 只向 `LanChat.Core.Tests` 开放 internals。

- [ ] **Step 4: 固定 DLL 解析路径**

`CoreLibraryResolver.Install(string appDirectory)` 只允许从 `appDirectory\lan_chat_core.dll` 加载：文件不存在时抛出含绝对路径的 `FileNotFoundException`，存在时调用 `NativeLibrary.Load`。只为 `LanChat.Core` 程序集的 `lan_chat_core.dll` 名称返回句柄，其他库返回 `nint.Zero`；重复安装同一路径无操作，尝试更换路径时抛出异常。

`LanChatCoreClient` 提供跨程序集可见的唯一生产入口：

```csharp
public static ILanChatCore Open(string appDirectory)
{
    CoreLibraryResolver.Install(appDirectory);
    return new LanChatCoreClient(new PInvokeLanChatCoreNative());
}
```

- [ ] **Step 5: 运行全部 Native 边界测试并提交**

Run:
```powershell
dotnet test winui_client/tests/LanChat.Core.Tests/LanChat.Core.Tests.csproj
git diff --check
git add winui_client/src/LanChat.Core winui_client/tests/LanChat.Core.Tests
git commit -m "feat: add WinUI core native boundary"
```
Expected: Native 边界测试全部通过，测试输出无警告。

### Task 3: 初始状态与事件 JSON 契约

**Files:**
- Create: `winui_client/src/LanChat.Core/Models/CoreSnapshot.cs`
- Create: `winui_client/src/LanChat.Core/Models/CoreEvent.cs`
- Create: `winui_client/tests/LanChat.Core.Tests/CoreSnapshotTests.cs`
- Create: `winui_client/tests/LanChat.Core.Tests/CoreEventTests.cs`

**Interfaces:**
- Consumes: schema version 1 的 Core 状态和 `state`/`result`/`error` 事件。
- Produces: `CoreSnapshot.Parse(string)`、`CoreEvent.Parse(string)`、`CoreEvent.RequiresSnapshotRefresh`。

- [ ] **Step 1: 写 JSON 契约失败测试**

```csharp
[TestMethod]
public void Parse_accepts_unknown_fields_and_reads_idle_connection()
{
    const string json = """
      {"schemaVersion":1,"connection":{"phase":"idle","statusText":"未连接"},"future":{"value":1}}
      """;

    var snapshot = CoreSnapshot.Parse(json);

    Assert.AreEqual(1, snapshot.SchemaVersion);
    Assert.AreEqual("idle", snapshot.ConnectionPhase);
    Assert.AreEqual("未连接", snapshot.StatusText);
}

[TestMethod]
public void Parse_rejects_unsupported_schema()
{
    Assert.ThrowsExactly<FormatException>(() =>
        CoreSnapshot.Parse("{\"schemaVersion\":2,\"connection\":{}}"));
}
```

事件测试断言 `{"kind":"state"}` 的 `RequiresSnapshotRefresh` 为 true，未知事件为 false，非 JSON 对象抛出 `FormatException`。

- [ ] **Step 2: 运行测试并确认 RED**

Run:
```powershell
dotnet test winui_client/tests/LanChat.Core.Tests/LanChat.Core.Tests.csproj --filter "FullyQualifiedName~CoreSnapshotTests|FullyQualifiedName~CoreEventTests"
```
Expected: 编译失败，提示 `CoreSnapshot` 和 `CoreEvent` 不存在。

- [ ] **Step 3: 实现最小强类型解析器**

`CoreSnapshot` 必须要求根对象、`schemaVersion == 1` 和 `connection` 对象；`phase` 缺失时使用 `idle`，`statusText` 缺失时使用 `未连接`，未知字段忽略：

```csharp
public sealed record CoreSnapshot(int SchemaVersion, string ConnectionPhase, string StatusText)
{
    public static CoreSnapshot Parse(string json)
    {
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object ||
            !root.TryGetProperty("schemaVersion", out var schema) ||
            schema.GetInt32() != 1)
        {
            throw new FormatException("不支持的 LAN Chat 状态版本。");
        }
        if (!root.TryGetProperty("connection", out var connection) ||
            connection.ValueKind != JsonValueKind.Object)
        {
            throw new FormatException("LAN Chat 状态缺少 connection 对象。");
        }
        var phase = connection.TryGetProperty("phase", out var phaseValue)
            ? phaseValue.GetString() ?? "idle" : "idle";
        var status = connection.TryGetProperty("statusText", out var statusValue)
            ? statusValue.GetString() ?? "未连接" : "未连接";
        return new CoreSnapshot(1, phase, status);
    }
}
```

`CoreEvent.Parse` 读取根对象的 `kind`，缺失时兼容 `type`；`RequiresSnapshotRefresh` 仅在 kind 为 `state` 时为 true。

- [ ] **Step 4: 运行契约测试和全量 Core 测试并提交**

Run:
```powershell
dotnet test winui_client/tests/LanChat.Core.Tests/LanChat.Core.Tests.csproj
git diff --check
git add winui_client/src/LanChat.Core/Models winui_client/tests/LanChat.Core.Tests
git commit -m "feat: parse WinUI core state and events"
```
Expected: 全部 Core 测试通过。

### Task 4: 专用线程 CoreRuntime

**Files:**
- Create: `winui_client/src/LanChat.Core/Runtime/ICoreRuntime.cs`
- Create: `winui_client/src/LanChat.Core/Runtime/CoreRuntime.cs`
- Create: `winui_client/tests/LanChat.Core.Tests/FakeLanChatCore.cs`
- Create: `winui_client/tests/LanChat.Core.Tests/CoreRuntimeTests.cs`

**Interfaces:**
- Consumes: `Func<ILanChatCore>` 和 100ms 事件轮询。
- Produces: `StartAsync()`、`DispatchAsync(string)`、`SetActive(bool)`、`StateChanged`、`RuntimeError`、`DisposeAsync()`。

- [ ] **Step 1: 写线程、轮询和生命周期失败测试**

测试必须断言：创建/状态/事件/命令/销毁使用同一个非测试线程；启动发布初始状态；非激活时 350ms 内不读取事件；恢复后立即读取；只有 state 事件触发新快照；销毁一次。

```csharp
[TestMethod]
public async Task All_core_calls_use_one_dedicated_thread()
{
    var fake = new FakeLanChatCore();
    await using var runtime = new CoreRuntime(() => fake, TimeSpan.FromMilliseconds(20));

    await runtime.StartAsync();
    await runtime.DispatchAsync("{\"id\":\"winui-1\",\"type\":\"test\",\"payload\":{}}");
    await runtime.DisposeAsync();

    Assert.AreEqual(1, fake.CallThreadIds.Distinct().Count());
    Assert.AreNotEqual(Environment.CurrentManagedThreadId, fake.CallThreadIds[0]);
}
```

- [ ] **Step 2: 运行测试并确认 RED**

Run:
```powershell
dotnet test winui_client/tests/LanChat.Core.Tests/LanChat.Core.Tests.csproj --filter "FullyQualifiedName~CoreRuntimeTests"
```
Expected: 编译失败，提示 `CoreRuntime` 不存在。

- [ ] **Step 3: 实现专用线程消息循环**

`CoreRuntime` 使用一个命名为 `LAN Chat Core` 的 `Thread`、`BlockingCollection<Action<ILanChatCore>>` 和 `CancellationTokenSource`。线程内部顺序固定：创建 Core、发布初始快照、循环处理命令、激活时按轮询间隔 DrainEvents、退出时 Dispose。所有 `TaskCompletionSource` 使用 `RunContinuationsAsynchronously`。

公开接口固定为：

```csharp
public interface ICoreRuntime : IAsyncDisposable
{
    event EventHandler<CoreSnapshot>? StateChanged;
    event EventHandler<Exception>? RuntimeError;
    Task StartAsync();
    Task<int> DispatchAsync(string commandJson);
    void SetActive(bool active);
}
```

`DrainEvents` 对每个 JSON 调用 `CoreEvent.Parse`；至少一个 state 事件才读取一次新快照。解析或 native 异常通过 `RuntimeError` 发布，循环继续保留最后有效状态；创建失败使 `StartAsync` 失败并结束线程。

- [ ] **Step 4: 运行 CoreRuntime 测试并提交**

Run:
```powershell
dotnet test winui_client/tests/LanChat.Core.Tests/LanChat.Core.Tests.csproj
git diff --check
git add winui_client/src/LanChat.Core/Runtime winui_client/tests/LanChat.Core.Tests
git commit -m "feat: add serialized WinUI core runtime"
```
Expected: 全部 Core 测试通过，无线程泄漏或测试超时。

### Task 5: 可测试的 ShellViewModel

**Files:**
- Create: `winui_client/src/LanChat.Presentation/IUiDispatcher.cs`
- Create: `winui_client/src/LanChat.Presentation/ShellViewModel.cs`
- Create: `winui_client/tests/LanChat.Presentation.Tests/ImmediateDispatcher.cs`
- Create: `winui_client/tests/LanChat.Presentation.Tests/FakeCoreRuntime.cs`
- Create: `winui_client/tests/LanChat.Presentation.Tests/ShellViewModelTests.cs`

**Interfaces:**
- Consumes: `ICoreRuntime` 状态与错误事件。
- Produces: `ShellViewModel.StatusText`、`ConnectionPhase`、`DiagnosticMessage`、`HasFatalError`、`StartAsync()`、`SetActive(bool)`。

- [ ] **Step 1: 写 ViewModel 失败测试**

```csharp
[TestMethod]
public async Task Start_shows_idle_snapshot_from_core()
{
    var runtime = new FakeCoreRuntime();
    var viewModel = new ShellViewModel(runtime, new ImmediateDispatcher());

    await viewModel.StartAsync();
    runtime.PublishState(new CoreSnapshot(1, "idle", "未连接"));

    Assert.AreEqual("idle", viewModel.ConnectionPhase);
    Assert.AreEqual("未连接", viewModel.StatusText);
    Assert.IsFalse(viewModel.HasFatalError);
}

[TestMethod]
public async Task Startup_failure_exposes_actionable_diagnostic()
{
    var runtime = new FakeCoreRuntime(new FileNotFoundException(
        "lan_chat_core.dll 未找到", @"C:\app\lan_chat_core.dll"));
    var viewModel = new ShellViewModel(runtime, new ImmediateDispatcher());

    await viewModel.StartAsync();

    Assert.IsTrue(viewModel.HasFatalError);
    StringAssert.Contains(viewModel.DiagnosticMessage, @"C:\app\lan_chat_core.dll");
}
```

- [ ] **Step 2: 运行测试并确认 RED**

Run:
```powershell
dotnet test winui_client/tests/LanChat.Presentation.Tests/LanChat.Presentation.Tests.csproj
```
Expected: 编译失败，提示 `ShellViewModel` 不存在。

- [ ] **Step 3: 实现最小 MVVM 状态**

`ShellViewModel` 实现 `INotifyPropertyChanged`。构造时订阅 runtime 事件；所有属性修改通过 `IUiDispatcher.Enqueue(Action)` 执行。初始 `StatusText` 为 `正在初始化…`、`ConnectionPhase` 为 `starting`；启动异常设置 `HasFatalError=true` 和异常完整消息，但不伪造 Core 状态。`DisposeAsync` 取消订阅并释放 runtime。

- [ ] **Step 4: 运行 Presentation 测试并提交**

Run:
```powershell
dotnet test winui_client/tests/LanChat.Presentation.Tests/LanChat.Presentation.Tests.csproj
git diff --check
git add winui_client/src/LanChat.Presentation winui_client/tests/LanChat.Presentation.Tests
git commit -m "feat: add WinUI shell view model"
```
Expected: Presentation 测试全部通过。

### Task 6: Fluent 石板紫 WinUI 应用壳

**Files:**
- Modify: `winui_client/src/LanChat.WinUI/App.xaml`
- Modify: `winui_client/src/LanChat.WinUI/App.xaml.cs`
- Modify: `winui_client/src/LanChat.WinUI/MainWindow.xaml`
- Modify: `winui_client/src/LanChat.WinUI/MainWindow.xaml.cs`
- Create: `winui_client/src/LanChat.WinUI/Styles/Theme.xaml`
- Create: `winui_client/src/LanChat.WinUI/DispatcherQueueAdapter.cs`

**Interfaces:**
- Consumes: `LanChatCoreClient.Open(AppContext.BaseDirectory)`、`CoreRuntime`、`ShellViewModel`。
- Produces: 显示 Core 初始状态或阻断式诊断的原生 WinUI 窗口。

- [ ] **Step 1: 写应用壳构建 RED**

先把 `MainWindow.xaml` 的 `x:Bind` 指向尚不存在的 `ViewModel.StatusText`、`ViewModel.HasFatalError` 和 `ViewModel.DiagnosticMessage`，并运行：

```powershell
dotnet build winui_client/src/LanChat.WinUI/LanChat.WinUI.csproj -c Debug
```
Expected: XAML/C# 编译失败，提示 `ViewModel` 或适配器尚未在窗口中定义。

- [ ] **Step 2: 实现主题资源**

`Styles/Theme.xaml` 定义石板紫强调色 `#625ACB`、深色标题栏 `#29263A`、卡片背景和 8px/12px 圆角资源；不得覆盖系统高对比度资源。`App.xaml` 合并 `XamlControlsResources` 和该主题字典。

- [ ] **Step 3: 实现窗口和诊断视图**

`MainWindow.xaml` 使用原生 `Grid`、`InfoBar` 和 `ProgressRing`：正常状态显示 LAN Chat 标题、`StatusText` 与“WinUI 3 前端阶段 1”说明；`HasFatalError` 时显示不可关闭的错误 `InfoBar`，正文绑定 `DiagnosticMessage`。窗口初始大小 1100×720，最小大小 900×620。

`DispatcherQueueAdapter.Enqueue` 调用窗口 `DispatcherQueue.TryEnqueue`，失败时抛出 `InvalidOperationException("无法调度 WinUI 状态更新。")`。

- [ ] **Step 4: 连接应用生命周期**

`App.OnLaunched` 创建 `MainWindow`。窗口构造时用 `() => LanChatCoreClient.Open(AppContext.BaseDirectory)` 创建 `CoreRuntime` 和 `ShellViewModel`。首次 `Activated` 调用一次 `StartAsync` 并 `SetActive(true)`；窗口失活时 `SetActive(false)`；`Closed` 中等待 `DisposeAsync`，不调用 `Environment.Exit`。

- [ ] **Step 5: 构建、运行单元测试并提交**

Run:
```powershell
dotnet test winui_client/LANChat.WinUI.slnx -c Debug --no-restore
dotnet build winui_client/src/LanChat.WinUI/LanChat.WinUI.csproj -c Debug --no-restore
git diff --check
git add winui_client/src/LanChat.WinUI
git commit -m "feat: add Fluent WinUI core shell"
```
Expected: 单元测试全通过，WinUI Debug 构建零警告、零错误。

### Task 7: 真实 Core、Release 发布与缩放验收

**Files:**
- Modify: `.gitignore`
- Modify: `docs/superpowers/plans/2026-08-28-winui3-phase1-core-shell.md`
- Create: `winui_client/build/NativeRuntime.targets`
- Modify: `winui_client/LANChat.WinUI.slnx`
- Modify: `winui_client/src/LanChat.WinUI/LanChat.WinUI.csproj`
- Create: `winui_client/tests/LanChat.Core.Native.Tests/LanChat.Core.Native.Tests.csproj`
- Create: `winui_client/tests/LanChat.Core.Native.Tests/MSTestSettings.cs`
- Create: `winui_client/tests/LanChat.Core.Native.Tests/RealCoreSmokeTests.cs`
- Create: `winui_client/tests/LanChat.Core.Native.Tests/packages.lock.json`
- Generate: `winui_client/artifacts/publish/win-x64/`

**Interfaces:**
- Consumes: `LanChatNativeRuntimeEnabled`、`LanChatCoreDll`、`LanChatQtPrefix`、`LanChatOpenSslRoot` MSBuild 属性及 `LAN_CHAT_CORE_NATIVE_TEST` 测试开关。
- Produces: 带 12 个目标 native DLL、可独立启动并显示真实初始状态的 WinUI Release 目录。

- [ ] **Step 1: 写真实 Core 受控失败测试**

`RealCoreSmokeTests` 位于独立的 `LanChat.Core.Native.Tests` MSTest 项目，仅在 `LAN_CHAT_CORE_NATIVE_TEST=1` 时运行；独立 testhost 避免与 `LanChat.Core.Tests` 中会安装测试 resolver 的单元测试共享进程状态。测试调用 resolver 和真实 client，断言快照 `schemaVersion == 1`、phase 为 `idle`，然后正常释放。未设置开关时使用 `Assert.Inconclusive`，不自动搜索其他目录的 DLL。

- [ ] **Step 2: 运行受控测试并确认 RED**

Run without copying runtime files:
```powershell
$env:LAN_CHAT_CORE_NATIVE_TEST = '1'
dotnet test winui_client/tests/LanChat.Core.Native.Tests/LanChat.Core.Native.Tests.csproj --filter "FullyQualifiedName~RealCoreSmokeTests"
```
Expected: FAIL，诊断明确指出测试输出目录缺少 `lan_chat_core.dll`。

- [ ] **Step 3: 实现严格 native 运行库复制**

`NativeRuntime.targets` 在无任何 native 属性且未显式启用时不参与普通 App/solution 构建。传入任一 native 路径属性或 `LanChatNativeRuntimeEnabled=true` 时自动启用，并要求三个路径属性全部非空；Native test 项目还会在 `LAN_CHAT_CORE_NATIVE_TEST=1` 时启用。启用后复制以下 12 个文件到 app/test 输出目录；任一源不是现有文件时 MSBuild 立即失败并显示绝对路径（现有目录也视为失败）。后四个文件是 Qt 6.10.3 中 `Qt6Quick.dll` 的必要传递依赖闭包，必须与其他 Qt DLL 来自同一 MSVC Qt prefix，不得从 PATH 搜索或回退到 MinGW 版本：

```text
lan_chat_core.dll
libssl-3-x64.dll
libcrypto-3-x64.dll
Qt6Core.dll
Qt6Gui.dll
Qt6Network.dll
Qt6Quick.dll
Qt6Qml.dll
Qt6QmlMeta.dll
Qt6QmlModels.dll
Qt6QmlWorkerScript.dll
Qt6OpenGL.dll
```

App 与独立的 Core native test 项目都导入该 targets。App 的 `PrepareForPublish` 另设发布契约：任何 publish 都必须显式传入三个路径属性，缺项立即失败；该门禁不影响普通无属性 Debug/solution build/test。Release publish/真实 Core 命令显式传入三个路径属性，不在仓库写死本机路径；未设置测试开关时 native smoke 为 Inconclusive。

- [ ] **Step 4: 运行真实 Core GREEN 和全量测试**

Run:
```powershell
$core = 'C:/Users/Q1573/Desktop/MY_project/lan-chat/out/flutter-core-vs2022/Release/lan_chat_core.dll'
$qt = 'C:/Users/Q1573/Desktop/MY_project/lan-chat/.tools/qt/6.10.3/msvc2022_64'
$openssl = 'C:/Users/Q1573/Desktop/MY_project/lan-chat/.tools/vcpkg/installed/x64-windows'
$common = @("-p:LanChatCoreDll=$core", "-p:LanChatQtPrefix=$qt", "-p:LanChatOpenSslRoot=$openssl")
$env:LAN_CHAT_CORE_NATIVE_TEST = '1'
dotnet test winui_client/LANChat.WinUI.slnx -c Release @common
```
Expected: 单元测试与真实 Core 测试全部通过，状态为 schema 1 / idle / 未连接。

- [ ] **Step 5: 发布非打包自包含 Release**

先确认无属性 publish 被拒绝，且 fresh 输出目录不留下可被误认为可运行的 EXE：

```powershell
dotnet publish winui_client/src/LanChat.WinUI/LanChat.WinUI.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=false -o winui_client/artifacts/publish/no-native
```

Expected: FAIL，明确指出缺少 `LanChatCoreDll`；输出目录不存在 `LanChat.WinUI.exe`。

Run:
```powershell
dotnet publish winui_client/src/LanChat.WinUI/LanChat.WinUI.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=false -p:LanChatCoreDll=$core -p:LanChatQtPrefix=$qt -p:LanChatOpenSslRoot=$openssl -o winui_client/artifacts/publish/win-x64
```
Expected: 输出目录包含 `LanChat.WinUI.exe`、Windows App SDK/.NET 自包含文件、项目 PRI、所有生成的 XBF、Assets 和 12 个目标 native DLL；发布门禁对缺失资源显示绝对路径并立即失败，不生成或安装 MSIX。

- [ ] **Step 6: 验证独立启动和运行库来源**

Run:

```powershell
$releaseDir = (Resolve-Path 'winui_client/artifacts/publish/win-x64').Path
$exe = Join-Path $releaseDir 'LanChat.WinUI.exe'
$targets = @('lan_chat_core.dll','libssl-3-x64.dll','libcrypto-3-x64.dll','Qt6Core.dll','Qt6Gui.dll','Qt6Network.dll','Qt6Quick.dll','Qt6Qml.dll','Qt6QmlMeta.dll','Qt6QmlModels.dll','Qt6QmlWorkerScript.dll','Qt6OpenGL.dll')
$process = Start-Process -FilePath $exe -WorkingDirectory $releaseDir -WindowStyle Hidden -PassThru
try {
  Start-Sleep -Seconds 3
  if ($process.HasExited) { throw "WinUI Release 提前退出，退出码 $($process.ExitCode)" }
  $loaded = @{}
  foreach ($module in $process.Modules) {
    if ($targets -contains $module.ModuleName) { $loaded[$module.ModuleName] = $module.FileName }
  }
  $missing = $targets | Where-Object { -not $loaded.ContainsKey($_) }
  $outside = $loaded.GetEnumerator() | Where-Object { [IO.Path]::GetDirectoryName($_.Value) -ne $releaseDir }
  "已加载目标运行库: $($loaded.Count)/$($targets.Count)"
  if ($missing) { throw "缺少目标运行库: $($missing -join ', ')" }
  if ($outside) { throw "存在目录外运行库: $($outside.Value -join ', ')" }
  winapp ui wait-for "未连接" -a LanChat.WinUI --value "未连接" --contains
} finally {
  if (-not $process.HasExited) { Stop-Process -Id $process.Id }
}
```

Expected: `已加载目标运行库: 12/12`；主窗口通过 UI Automation 可读取 `未连接`，没有 Core 诊断错误。

- [ ] **Step 7: 自动缩放性能检查**

先再次启动 Release，然后运行：

```powershell
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class WinUiResizeNative {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int width, int height, bool repaint);
  public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
'@
$process = Start-Process -FilePath $exe -WorkingDirectory $releaseDir -WindowStyle Hidden -PassThru
$original = New-Object WinUiResizeNative+RECT
try {
  Start-Sleep -Seconds 3
  $process.Refresh()
  if ($process.HasExited -or $process.MainWindowHandle -eq 0) { throw '本次 WinUI Release 未创建窗口' }
  $handle = $process.MainWindowHandle
  [WinUiResizeNative]::GetWindowRect($handle, [ref]$original) | Out-Null
  $durations = [Collections.Generic.List[double]]::new()
  $total = [Diagnostics.Stopwatch]::StartNew()
  for ($index = 0; $index -lt 120; $index++) {
    if ($index % 2 -eq 0) { $width = 900; $height = 620 } else { $width = 1380; $height = 860 }
    $step = [Diagnostics.Stopwatch]::StartNew()
    [WinUiResizeNative]::MoveWindow($handle, $original.Left, $original.Top, $width, $height, $true) | Out-Null
    $step.Stop()
    $durations.Add($step.Elapsed.TotalMilliseconds)
    Start-Sleep -Milliseconds 40
  }
  [WinUiResizeNative]::MoveWindow($handle, $original.Left, $original.Top, $original.Right - $original.Left, $original.Bottom - $original.Top, $true) | Out-Null
  $total.Stop()
  $sorted = $durations | Sort-Object
  $p95 = $sorted[[math]::Ceiling($sorted.Count * 0.95) - 1]
  "Resize total: $($total.Elapsed.TotalSeconds)s"
  "MoveWindow P95: $p95 ms"
  if ($total.Elapsed.TotalSeconds -gt 8) { throw 'WinUI 自动缩放总耗时超过 8 秒' }
  if ($p95 -gt 33) { throw 'WinUI MoveWindow P95 超过 33ms' }
} finally {
  if ($handle) { [WinUiResizeNative]::MoveWindow($handle, $original.Left, $original.Top, $original.Right - $original.Left, $original.Bottom - $original.Top, $true) | Out-Null }
  if (-not $process.HasExited) { Stop-Process -Id $process.Id }
}
```

Expected: 总耗时不超过 8 秒，单次调用 P95 不超过 33ms；若失败，停止阶段验收并按系统化调试处理，不修改 UI 来掩盖问题。

- [ ] **Step 8: 全量回归、提交与交付**

Run:
```powershell
dotnet test winui_client/LANChat.WinUI.slnx -c Release @common
C:/Users/Q1573/Documents/Codex/2026-08-18/wei/tools/cmake/bin/ctest.exe --test-dir out/flutter-core-vs2022 -C Release -R lan-chat-core --output-on-failure
git diff --check
git status --short --branch
```
Expected: WinUI 测试全部通过；既有 Core 2/2 通过；功能分支只包含阶段 1 文件；主工作树仍只有用户的 `LANChat-Launcher.exe` 未跟踪。

Commit:
```powershell
git add .gitignore docs/superpowers/plans/2026-08-28-winui3-phase1-core-shell.md winui_client
git commit -m "build: package WinUI core shell"
```

向用户提供：

```text
C:/Users/Q1573/Desktop/MY_project/lan-chat/.worktrees/winui3-phase1-core-shell/winui_client/artifacts/publish/win-x64/LanChat.WinUI.exe
```

请用户手动确认窗口打开、显示“未连接”、四边与四角缩放流畅。该确认完成后阶段 1 才算验收，通过后另写阶段 2 连接流程计划。

## Authoritative References

- Windows App SDK 2.4.0 stable: https://learn.microsoft.com/windows/apps/windows-app-sdk/downloads
- Unpackaged WinUI 3: https://learn.microsoft.com/windows/apps/package-and-deploy/unpackage-winui-app
- Self-contained deployment: https://learn.microsoft.com/windows/apps/package-and-deploy/self-contained-deploy/deploy-self-contained-apps
- Official WinApp CLI: https://github.com/microsoft/WinAppCli
- .NET 10 SDK 10.0.400: https://dotnet.microsoft.com/download/dotnet/10.0
