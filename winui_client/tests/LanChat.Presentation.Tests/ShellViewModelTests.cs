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
    public async Task Synchronous_start_failure_is_captured_as_fatal_diagnostic()
    {
        var viewModel = new ShellViewModel(
            new FakeCoreRuntime(
                startException: new FileNotFoundException("同步缺失", @"C:\\app\\lan_chat_core.dll"),
                throwDuringStart: true),
            new ImmediateDispatcher());

        await viewModel.StartAsync();

        Assert.IsTrue(viewModel.HasFatalError);
        StringAssert.Contains(viewModel.DiagnosticMessage, "同步缺失");
    }

    [TestMethod]
    public async Task Startup_dispatcher_failure_faults_shared_start_task()
    {
        var viewModel = new ShellViewModel(
            new FakeCoreRuntime(startException: new InvalidOperationException("启动失败")),
            new ThrowingDispatcher());

        await Assert.ThrowsAsync<InvalidOperationException>(
            async () => await viewModel.StartAsync().WaitAsync(TimeSpan.FromSeconds(1)));
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
    public async Task Dispose_prevents_deferred_runtime_error_from_running()
    {
        var runtime = new FakeCoreRuntime();
        var dispatcher = new DeferredDispatcher();
        var viewModel = new ShellViewModel(runtime, dispatcher);
        runtime.PublishError(new InvalidOperationException("延迟错误"));

        var dispose = viewModel.DisposeAsync().AsTask();
        Assert.IsFalse(dispose.Wait(TimeSpan.FromMilliseconds(100)));
        dispatcher.Drain();
        await dispose.WaitAsync(TimeSpan.FromSeconds(1));

        Assert.IsNull(viewModel.DiagnosticMessage);
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
    public async Task SetActive_after_dispose_throws_object_disposed()
    {
        var viewModel = new ShellViewModel(new FakeCoreRuntime(), new ImmediateDispatcher());
        await viewModel.DisposeAsync();

        Assert.Throws<ObjectDisposedException>(() => viewModel.SetActive(true));
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
        Assert.AreEqual(0, runtime.StateChangedSubscriberCount);
        Assert.AreEqual(0, runtime.RuntimeErrorSubscriberCount);
        Assert.AreEqual("starting", viewModel.ConnectionPhase);
        Assert.IsNull(viewModel.DiagnosticMessage);
    }

    [TestMethod]
    public async Task Dispose_prevents_deferred_state_update_from_running()
    {
        var runtime = new FakeCoreRuntime();
        var dispatcher = new DeferredDispatcher();
        var viewModel = new ShellViewModel(runtime, dispatcher);
        runtime.PublishState(new CoreSnapshot(1, "idle", "未连接"));

        Assert.AreEqual(1, dispatcher.PendingCount);
        var dispose = viewModel.DisposeAsync().AsTask();
        Assert.IsFalse(dispose.Wait(TimeSpan.FromMilliseconds(100)));
        dispatcher.Drain();
        await dispose.WaitAsync(TimeSpan.FromSeconds(1));

        Assert.AreEqual("starting", viewModel.ConnectionPhase);
        Assert.AreEqual("正在初始化…", viewModel.StatusText);
    }

    [TestMethod]
    public async Task Dispose_waits_for_started_property_notification()
    {
        var runtime = new FakeCoreRuntime();
        var viewModel = new ShellViewModel(runtime, new BackgroundDispatcher());
        using var notificationStarted = new ManualResetEventSlim();
        using var releaseNotification = new ManualResetEventSlim();
        viewModel.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName == nameof(ShellViewModel.ConnectionPhase))
            {
                notificationStarted.Set();
                releaseNotification.Wait();
            }
        };

        runtime.PublishState(new CoreSnapshot(1, "idle", "未连接"));
        Assert.IsTrue(notificationStarted.Wait(TimeSpan.FromSeconds(1)));
        Assert.AreEqual("idle", viewModel.ConnectionPhase);
        var dispose = viewModel.DisposeAsync().AsTask();

        Assert.IsFalse(dispose.Wait(TimeSpan.FromMilliseconds(100)));
        releaseNotification.Set();
        await dispose.WaitAsync(TimeSpan.FromSeconds(1));
    }

    [TestMethod]
    public void Reentrant_dispose_stops_remaining_state_property_updates()
    {
        var runtime = new FakeCoreRuntime();
        var viewModel = new ShellViewModel(runtime, new ImmediateDispatcher());
        viewModel.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName == nameof(ShellViewModel.ConnectionPhase))
            {
                _ = viewModel.DisposeAsync();
            }
        };

        runtime.PublishState(new CoreSnapshot(1, "idle", "未连接"));

        Assert.AreEqual("idle", viewModel.ConnectionPhase);
        Assert.AreEqual("正在初始化…", viewModel.StatusText);
    }

    [TestMethod]
    public async Task Start_after_dispose_throws_object_disposed()
    {
        var viewModel = new ShellViewModel(new FakeCoreRuntime(), new ImmediateDispatcher());
        await viewModel.DisposeAsync();

        await Assert.ThrowsAsync<ObjectDisposedException>(async () => await viewModel.StartAsync());
    }

    [TestMethod]
    public async Task Concurrent_dispose_makes_delayed_start_fail_as_disposed()
    {
        var runtime = new FakeCoreRuntime(delayStart: true);
        var viewModel = new ShellViewModel(runtime, new ImmediateDispatcher());
        var start = viewModel.StartAsync();
        Assert.IsTrue(runtime.WaitForStart(TimeSpan.FromSeconds(1)));

        await viewModel.DisposeAsync();

        await Assert.ThrowsAsync<ObjectDisposedException>(async () => await start);
        Assert.AreEqual(1, runtime.StartCount);
        Assert.AreEqual(1, runtime.DisposeCount);
    }

    [TestMethod]
    public async Task Concurrent_start_calls_share_one_runtime_start()
    {
        var runtime = new FakeCoreRuntime(delayStart: true);
        var viewModel = new ShellViewModel(runtime, new ImmediateDispatcher());
        var firstStart = viewModel.StartAsync();
        var secondStart = viewModel.StartAsync();

        Assert.IsTrue(runtime.WaitForStart(TimeSpan.FromSeconds(1)));
        Assert.AreEqual(1, runtime.StartCount);
        await viewModel.DisposeAsync();

        await Assert.ThrowsAsync<ObjectDisposedException>(async () => await firstStart);
        await Assert.ThrowsAsync<ObjectDisposedException>(async () => await secondStart);
    }

    [TestMethod]
    public async Task Synchronously_blocking_start_does_not_block_dispose()
    {
        var runtime = new FakeCoreRuntime(blockStartSynchronously: true);
        var viewModel = new ShellViewModel(runtime, new ImmediateDispatcher());
        var startInvocation = Task.Run(viewModel.StartAsync);
        Assert.IsTrue(runtime.WaitForBlockedStart(TimeSpan.FromSeconds(1)));
        var dispose = Task.Run(async () => await viewModel.DisposeAsync());

        try
        {
            await dispose.WaitAsync(TimeSpan.FromSeconds(1));
        }
        finally
        {
            runtime.ReleaseBlockedStart();
            await dispose.WaitAsync(TimeSpan.FromSeconds(1));
        }

        await Assert.ThrowsAsync<ObjectDisposedException>(async () => await startInvocation);
    }

    [TestMethod]
    public async Task Property_changed_subscriber_can_wait_for_dispose_without_deadlock()
    {
        var runtime = new FakeCoreRuntime();
        var viewModel = new ShellViewModel(runtime, new ImmediateDispatcher());
        Task? dispose = null;
        var subscriberObservedCompletion = false;
        viewModel.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName == nameof(ShellViewModel.ConnectionPhase))
            {
                dispose = Task.Run(async () => await viewModel.DisposeAsync());
                subscriberObservedCompletion = dispose.Wait(TimeSpan.FromSeconds(1));
            }
        };

        runtime.PublishState(new CoreSnapshot(1, "idle", "未连接"));

        Assert.IsTrue(subscriberObservedCompletion);
        await dispose!.WaitAsync(TimeSpan.FromSeconds(1));
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

    [TestMethod]
    public async Task Asynchronous_dispose_failure_is_not_retried()
    {
        var runtime = new FakeCoreRuntime(disposeException: new InvalidOperationException("异步释放失败"));
        var viewModel = new ShellViewModel(runtime, new ImmediateDispatcher());

        await Assert.ThrowsAsync<InvalidOperationException>(async () => await viewModel.DisposeAsync());
        await Assert.ThrowsAsync<InvalidOperationException>(async () => await viewModel.DisposeAsync());

        Assert.AreEqual(1, runtime.DisposeCount);
    }
}
