using LanChat.Core.Models;
using LanChat.Presentation;

namespace LanChat.Presentation.Tests;

[TestClass]
public sealed class ShellViewModelTests
{
    [TestMethod]
    public void Constructor_exposes_starting_state()
    {
        var viewModel = new ShellViewModel(new FakeCoreRuntime(), new ImmediateDispatcher());

        Assert.AreEqual("starting", viewModel.ConnectionPhase);
        Assert.AreEqual("正在初始化…", viewModel.StatusText);
        Assert.IsFalse(viewModel.HasFatalError);
        Assert.IsNull(viewModel.DiagnosticMessage);
    }

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
            "lan_chat_core.dll 未找到", @"C:\\app\\lan_chat_core.dll"));
        var viewModel = new ShellViewModel(runtime, new ImmediateDispatcher());

        await viewModel.StartAsync();

        Assert.IsTrue(viewModel.HasFatalError);
        StringAssert.Contains(viewModel.DiagnosticMessage, @"C:\\app\\lan_chat_core.dll");
        StringAssert.Contains(viewModel.DiagnosticMessage, "lan_chat_core.dll 未找到");
        Assert.AreEqual("starting", viewModel.ConnectionPhase);
    }

    [TestMethod]
    public void State_updates_are_dispatched_and_only_raise_changed_properties()
    {
        var runtime = new FakeCoreRuntime();
        var dispatcher = new ImmediateDispatcher();
        var viewModel = new ShellViewModel(runtime, dispatcher);
        var propertyNames = new List<string?>();
        viewModel.PropertyChanged += (_, args) => propertyNames.Add(args.PropertyName);

        runtime.PublishState(new CoreSnapshot(1, "idle", "未连接"));
        runtime.PublishState(new CoreSnapshot(1, "idle", "未连接"));

        Assert.AreEqual(2, dispatcher.EnqueueCount);
        CollectionAssert.AreEqual(
            new[] { nameof(ShellViewModel.ConnectionPhase), nameof(ShellViewModel.StatusText) },
            propertyNames);
    }

    [TestMethod]
    public void Runtime_error_is_dispatched_without_rewriting_core_state()
    {
        var runtime = new FakeCoreRuntime();
        var dispatcher = new ImmediateDispatcher();
        var viewModel = new ShellViewModel(runtime, dispatcher);
        runtime.PublishState(new CoreSnapshot(1, "connected", "已连接"));

        runtime.PublishError(new InvalidOperationException("轮询失败"));

        Assert.AreEqual(2, dispatcher.EnqueueCount);
        Assert.AreEqual("connected", viewModel.ConnectionPhase);
        Assert.AreEqual("已连接", viewModel.StatusText);
        Assert.IsFalse(viewModel.HasFatalError);
        StringAssert.Contains(viewModel.DiagnosticMessage, "轮询失败");
    }

    [TestMethod]
    public void SetActive_passes_value_to_runtime()
    {
        var runtime = new FakeCoreRuntime();
        var viewModel = new ShellViewModel(runtime, new ImmediateDispatcher());

        viewModel.SetActive(true);

        Assert.IsTrue(runtime.LastSetActive);
    }

    [TestMethod]
    public async Task Dispose_unsubscribes_and_disposes_runtime_once()
    {
        var runtime = new FakeCoreRuntime();
        var viewModel = new ShellViewModel(runtime, new ImmediateDispatcher());

        await viewModel.DisposeAsync();
        await viewModel.DisposeAsync();
        runtime.PublishState(new CoreSnapshot(1, "idle", "未连接"));
        runtime.PublishError(new InvalidOperationException("ignored"));

        Assert.AreEqual(1, runtime.DisposeCount);
        Assert.AreEqual("starting", viewModel.ConnectionPhase);
        Assert.IsNull(viewModel.DiagnosticMessage);
    }

    [TestMethod]
    public async Task Dispose_failure_is_not_retried()
    {
        var runtime = new FakeCoreRuntime(
            disposeException: new InvalidOperationException("释放失败"),
            throwDuringDispose: true);
        var viewModel = new ShellViewModel(runtime, new ImmediateDispatcher());

        await Assert.ThrowsAsync<InvalidOperationException>(async () => await viewModel.DisposeAsync());
        await Assert.ThrowsAsync<InvalidOperationException>(async () => await viewModel.DisposeAsync());

        Assert.AreEqual(1, runtime.DisposeCount);
    }
}
