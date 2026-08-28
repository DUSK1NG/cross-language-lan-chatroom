using LanChat.Core.Runtime;

namespace LanChat.Core.Tests;

[TestClass]
public sealed class CoreRuntimeTests
{
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(3);

    [TestMethod]
    public async Task All_core_calls_use_one_named_dedicated_thread()
    {
        var testThreadId = Environment.CurrentManagedThreadId;
        var factoryThreadId = 0;
        string? factoryThreadName = null;
        var fake = new FakeLanChatCore();
        await using var runtime = new CoreRuntime(() =>
        {
            factoryThreadId = Environment.CurrentManagedThreadId;
            factoryThreadName = Thread.CurrentThread.Name;
            return fake;
        }, TimeSpan.FromMilliseconds(20));

        await runtime.StartAsync().WaitAsync(Timeout);
        runtime.SetActive(true);
        await WaitUntilAsync(() => fake.DrainEventsCallCount > 0);
        await runtime.DispatchAsync("{\"id\":\"winui-1\",\"type\":\"test\",\"payload\":{}}")
            .WaitAsync(Timeout);
        await runtime.DisposeAsync().AsTask().WaitAsync(Timeout);

        var callThreadIds = fake.CallThreadIds.Append(factoryThreadId).Distinct().ToArray();
        Assert.HasCount(1, callThreadIds);
        Assert.AreNotEqual(testThreadId, callThreadIds[0]);
        Assert.AreEqual("LAN Chat Core", factoryThreadName);
        Assert.IsTrue(fake.Calls.All(call => call.ThreadName == "LAN Chat Core"));
    }

    [TestMethod]
    public async Task Start_publishes_initial_snapshot_on_core_thread()
    {
        var fake = new FakeLanChatCore();
        await using var runtime = new CoreRuntime(() => fake, TimeSpan.FromMilliseconds(20));
        var changed = new TaskCompletionSource<(Core.Models.CoreSnapshot Snapshot, int ThreadId)>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        runtime.StateChanged += (_, snapshot) =>
            changed.TrySetResult((snapshot, Environment.CurrentManagedThreadId));

        await runtime.StartAsync().WaitAsync(Timeout);
        var published = await changed.Task.WaitAsync(Timeout);

        Assert.AreEqual("idle", published.Snapshot.ConnectionPhase);
        Assert.AreEqual(fake.CallThreadIds.Single(), published.ThreadId);
    }

    [TestMethod]
    public async Task Inactive_runtime_does_not_drain_and_reactivation_drains_immediately()
    {
        var fake = new FakeLanChatCore();
        await using var runtime = new CoreRuntime(() => fake, TimeSpan.FromSeconds(2));
        var snapshots = new System.Collections.Concurrent.ConcurrentQueue<Core.Models.CoreSnapshot>();
        runtime.StateChanged += (_, snapshot) => snapshots.Enqueue(snapshot);

        await runtime.StartAsync().WaitAsync(Timeout);
        await Task.Delay(TimeSpan.FromMilliseconds(350));
        Assert.AreEqual(0, fake.DrainEventsCallCount);

        fake.EnqueueEvent("{\"kind\":\"state\"}");
        fake.EnqueueState("{\"schemaVersion\":1,\"connection\":{\"phase\":\"connected\",\"statusText\":\"在线\"}}");
        var reactivationStarted = System.Diagnostics.Stopwatch.GetTimestamp();
        runtime.SetActive(true);

        await WaitUntilAsync(() => snapshots.Count == 2);
        Assert.IsLessThan(
            TimeSpan.FromMilliseconds(1500),
            System.Diagnostics.Stopwatch.GetElapsedTime(reactivationStarted));
        Assert.AreEqual("connected", snapshots.ToArray()[1].ConnectionPhase);
    }

    [TestMethod]
    public async Task One_drain_with_multiple_state_events_refreshes_snapshot_once()
    {
        var fake = new FakeLanChatCore();
        fake.EnqueueEvent("{\"kind\":\"state\"}");
        fake.EnqueueEvent("{\"kind\":\"result\"}");
        fake.EnqueueEvent("{\"kind\":\"state\"}");
        fake.EnqueueState("{\"schemaVersion\":1,\"connection\":{\"phase\":\"connected\"}}");
        await using var runtime = new CoreRuntime(() => fake, TimeSpan.FromMilliseconds(20));
        var publishCount = 0;
        runtime.StateChanged += (_, _) => Interlocked.Increment(ref publishCount);

        await runtime.StartAsync().WaitAsync(Timeout);
        runtime.SetActive(true);
        await WaitUntilAsync(() => fake.DrainEventsCallCount >= 2);

        Assert.AreEqual(2, fake.ReadStateCallCount);
        Assert.AreEqual(2, publishCount);
    }

    [TestMethod]
    public async Task Result_error_and_unknown_events_do_not_refresh_snapshot()
    {
        var fake = new FakeLanChatCore();
        fake.EnqueueEvent("{\"kind\":\"result\"}");
        fake.EnqueueEvent("{\"kind\":\"error\"}");
        fake.EnqueueEvent("{\"kind\":\"future\"}");
        await using var runtime = new CoreRuntime(() => fake, TimeSpan.FromMilliseconds(20));

        await runtime.StartAsync().WaitAsync(Timeout);
        runtime.SetActive(true);
        await WaitUntilAsync(() => fake.DrainEventsCallCount >= 2);

        Assert.AreEqual(1, fake.ReadStateCallCount);
    }

    [TestMethod]
    public async Task Invalid_event_reports_error_and_later_state_event_still_refreshes()
    {
        var fake = new FakeLanChatCore();
        fake.EnqueueEvent("{");
        fake.EnqueueEvent("{\"kind\":\"state\"}");
        fake.EnqueueState("{\"schemaVersion\":1,\"connection\":{\"phase\":\"connected\"}}");
        await using var runtime = new CoreRuntime(() => fake, TimeSpan.FromMilliseconds(20));
        var errors = new System.Collections.Concurrent.ConcurrentQueue<Exception>();
        var publishCount = 0;
        runtime.RuntimeError += (_, error) => errors.Enqueue(error);
        runtime.StateChanged += (_, _) => Interlocked.Increment(ref publishCount);

        await runtime.StartAsync().WaitAsync(Timeout);
        runtime.SetActive(true);
        await WaitUntilAsync(() => errors.Count == 1 && publishCount == 2);

        Assert.IsInstanceOfType<FormatException>(errors.Single());
        Assert.AreEqual(0, fake.DisposeCallCount, "运行时不应因解析错误提前销毁核心。");
    }

    [TestMethod]
    public async Task Native_poll_and_snapshot_errors_report_error_and_loop_keeps_last_snapshot()
    {
        var fake = new FakeLanChatCore();
        await using var runtime = new CoreRuntime(() => fake, TimeSpan.FromMilliseconds(20));
        var errors = new System.Collections.Concurrent.ConcurrentQueue<Exception>();
        var snapshots = new System.Collections.Concurrent.ConcurrentQueue<Core.Models.CoreSnapshot>();
        runtime.RuntimeError += (_, error) => errors.Enqueue(error);
        runtime.StateChanged += (_, snapshot) => snapshots.Enqueue(snapshot);

        await runtime.StartAsync().WaitAsync(Timeout);
        fake.FailNextDrainEvents(new InvalidOperationException("drain failed"));
        fake.FailNextReadState(new InvalidOperationException("state failed"));
        fake.EnqueueEvent("{\"kind\":\"state\"}");
        runtime.SetActive(true);
        await WaitUntilAsync(() => errors.Count == 2);
        Assert.HasCount(1, snapshots);

        fake.EnqueueEvent("{\"kind\":\"state\"}");
        fake.EnqueueState("not-json");
        await WaitUntilAsync(() => errors.Count == 3);
        Assert.HasCount(1, snapshots);

        fake.EnqueueEvent("{\"kind\":\"state\"}");
        fake.EnqueueState("{\"schemaVersion\":1,\"connection\":{\"phase\":\"connected\"}}");
        await WaitUntilAsync(() => snapshots.Count == 2);
        Assert.AreEqual("connected", snapshots.Last().ConnectionPhase);
    }

    [TestMethod]
    public async Task Dispatch_error_faults_command_reports_error_and_next_command_runs()
    {
        var fake = new FakeLanChatCore { DispatchResult = 17 };
        await using var runtime = new CoreRuntime(() => fake, TimeSpan.FromMilliseconds(20));
        var error = new TaskCompletionSource<Exception>(TaskCreationOptions.RunContinuationsAsynchronously);
        runtime.RuntimeError += (_, exception) => error.TrySetResult(exception);

        await runtime.StartAsync().WaitAsync(Timeout);
        fake.FailNextDispatch(new InvalidOperationException("dispatch failed"));
        await Assert.ThrowsExactlyAsync<InvalidOperationException>(
            () => runtime.DispatchAsync("first").WaitAsync(Timeout));
        Assert.AreEqual("dispatch failed", (await error.Task.WaitAsync(Timeout)).Message);
        Assert.AreEqual(17, await runtime.DispatchAsync("second").WaitAsync(Timeout));
    }

    [TestMethod]
    public async Task Factory_failure_faults_start_reports_error_and_dispose_finishes()
    {
        var failure = new InvalidOperationException("create failed");
        await using var runtime = new CoreRuntime(() => throw failure, TimeSpan.FromMilliseconds(20));
        var reported = new TaskCompletionSource<(Exception Error, string? ThreadName)>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        runtime.RuntimeError += (_, error) => reported.TrySetResult((error, Thread.CurrentThread.Name));

        var thrown = await Assert.ThrowsExactlyAsync<InvalidOperationException>(
            () => runtime.StartAsync().WaitAsync(Timeout));
        var observed = await reported.Task.WaitAsync(Timeout);
        await runtime.DisposeAsync().AsTask().WaitAsync(Timeout);

        Assert.AreSame(failure, thrown);
        Assert.AreSame(failure, observed.Error);
        Assert.AreEqual("LAN Chat Core", observed.ThreadName);
    }

    [TestMethod]
    public async Task Dispose_is_idempotent_and_runs_after_accepted_command()
    {
        using var dispatchGate = new ManualResetEventSlim(false);
        var fake = new FakeLanChatCore { DispatchGate = dispatchGate, DispatchResult = 5 };
        var runtime = new CoreRuntime(() => fake, TimeSpan.FromMilliseconds(20));
        await runtime.StartAsync().WaitAsync(Timeout);

        var command = runtime.DispatchAsync("accepted");
        await WaitUntilAsync(() => fake.Calls.Any(call => call.Operation == nameof(ILanChatCore.Dispatch)));
        var firstDispose = runtime.DisposeAsync().AsTask();
        var secondDispose = runtime.DisposeAsync().AsTask();
        Assert.ThrowsExactly<ObjectDisposedException>(() => runtime.DispatchAsync("rejected"));
        Assert.ThrowsExactly<ObjectDisposedException>(() => runtime.SetActive(true));
        Assert.ThrowsExactly<ObjectDisposedException>(() => runtime.StartAsync());

        dispatchGate.Set();
        Assert.AreEqual(5, await command.WaitAsync(Timeout));
        await Task.WhenAll(firstDispose, secondDispose).WaitAsync(Timeout);

        Assert.AreEqual(1, fake.DisposeCallCount);
        var operations = fake.Calls.Select(call => call.Operation).ToArray();
        Assert.IsLessThan(
            Array.IndexOf(operations, nameof(ILanChatCore.Dispose)),
            Array.IndexOf(operations, nameof(ILanChatCore.Dispatch)));
    }

    [TestMethod]
    public async Task Dispose_before_start_prevents_all_later_operations_without_creating_core()
    {
        var factoryCallCount = 0;
        var fake = new FakeLanChatCore();
        var runtime = new CoreRuntime(() =>
        {
            Interlocked.Increment(ref factoryCallCount);
            return fake;
        });

        Assert.ThrowsExactly<InvalidOperationException>(() => runtime.DispatchAsync("before-start"));
        Assert.ThrowsExactly<InvalidOperationException>(() => runtime.SetActive(true));
        await runtime.DisposeAsync().AsTask().WaitAsync(Timeout);
        await runtime.DisposeAsync().AsTask().WaitAsync(Timeout);
        Assert.ThrowsExactly<ObjectDisposedException>(() => runtime.StartAsync());
        Assert.ThrowsExactly<ObjectDisposedException>(() => runtime.DispatchAsync("after-dispose"));
        Assert.ThrowsExactly<ObjectDisposedException>(() => runtime.SetActive(false));

        Assert.AreEqual(0, factoryCallCount);
        Assert.AreEqual(0, fake.DisposeCallCount);
    }

    [TestMethod]
    public async Task Dispose_failure_is_reported_faults_all_dispose_calls_and_destroys_once()
    {
        var failure = new InvalidOperationException("dispose failed");
        var fake = new FakeLanChatCore();
        fake.FailNextDispose(failure);
        var runtime = new CoreRuntime(() => fake);
        var reported = new TaskCompletionSource<Exception>(TaskCreationOptions.RunContinuationsAsynchronously);
        runtime.RuntimeError += (_, error) => reported.TrySetResult(error);
        await runtime.StartAsync().WaitAsync(Timeout);

        var first = await Assert.ThrowsExactlyAsync<InvalidOperationException>(
            () => runtime.DisposeAsync().AsTask().WaitAsync(Timeout));
        var second = await Assert.ThrowsExactlyAsync<InvalidOperationException>(
            () => runtime.DisposeAsync().AsTask().WaitAsync(Timeout));

        Assert.AreSame(failure, first);
        Assert.AreSame(failure, second);
        Assert.AreSame(failure, await reported.Task.WaitAsync(Timeout));
        Assert.AreEqual(1, fake.DisposeCallCount);
    }

    [TestMethod]
    public async Task Start_dispatch_and_dispose_continuations_do_not_run_inline_on_core_thread()
    {
        using var readGate = new ManualResetEventSlim(false);
        using var dispatchGate = new ManualResetEventSlim(false);
        using var disposeGate = new ManualResetEventSlim(false);
        var fake = new FakeLanChatCore
        {
            ReadStateGate = readGate,
            DispatchGate = dispatchGate,
            DisposeGate = disposeGate,
        };
        var runtime = new CoreRuntime(() => fake);

        var start = runtime.StartAsync();
        var startContinuation = RecordSynchronousContinuationThread(start);
        readGate.Set();
        await start.WaitAsync(Timeout);
        var coreThreadId = fake.CallThreadIds.Single();

        var dispatch = runtime.DispatchAsync("command");
        var dispatchContinuation = RecordSynchronousContinuationThread(dispatch);
        dispatchGate.Set();
        await dispatch.WaitAsync(Timeout);

        var dispose = runtime.DisposeAsync().AsTask();
        var disposeContinuation = RecordSynchronousContinuationThread(dispose);
        disposeGate.Set();
        await dispose.WaitAsync(Timeout);

        Assert.AreNotEqual(coreThreadId, await startContinuation.WaitAsync(Timeout));
        Assert.AreNotEqual(coreThreadId, await dispatchContinuation.WaitAsync(Timeout));
        Assert.AreNotEqual(coreThreadId, await disposeContinuation.WaitAsync(Timeout));
    }

    [TestMethod]
    public async Task Dispose_during_start_waits_for_initialization_and_duplicate_start_shares_task()
    {
        using var readGate = new ManualResetEventSlim(false);
        var fake = new FakeLanChatCore { ReadStateGate = readGate };
        var runtime = new CoreRuntime(() => fake);

        var firstStart = runtime.StartAsync();
        var secondStart = runtime.StartAsync();
        Assert.AreSame(firstStart, secondStart);
        await WaitUntilAsync(() => fake.ReadStateCallCount == 1);
        var dispose = runtime.DisposeAsync().AsTask();

        readGate.Set();
        await firstStart.WaitAsync(Timeout);
        await dispose.WaitAsync(Timeout);

        Assert.AreEqual(1, fake.ReadStateCallCount);
        Assert.AreEqual(1, fake.DisposeCallCount);
    }

    [TestMethod]
    public async Task Dispatch_set_active_and_dispose_race_completes_or_rejects_each_call()
    {
        var fake = new FakeLanChatCore { DispatchResult = 23 };
        var runtime = new CoreRuntime(() => fake, TimeSpan.FromMilliseconds(20));
        await runtime.StartAsync().WaitAsync(Timeout);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        var commands = Enumerable.Range(0, 20).Select(index => Task.Run(async () =>
        {
            await release.Task;
            try
            {
                return await runtime.DispatchAsync($"command-{index}");
            }
            catch (ObjectDisposedException)
            {
                return -1;
            }
        })).ToArray();
        var activations = Enumerable.Range(0, 20).Select(index => Task.Run(async () =>
        {
            await release.Task;
            try
            {
                runtime.SetActive(index % 2 == 0);
                return true;
            }
            catch (ObjectDisposedException)
            {
                return false;
            }
        })).ToArray();
        var dispose = Task.Run(async () =>
        {
            await release.Task;
            await runtime.DisposeAsync();
        });

        release.SetResult();
        var results = await Task.WhenAll(commands).WaitAsync(Timeout);
        await Task.WhenAll(activations).WaitAsync(Timeout);
        await dispose.WaitAsync(Timeout);

        Assert.IsTrue(results.All(result => result is 23 or -1));
        Assert.AreEqual(results.Count(result => result == 23),
            fake.Calls.Count(call => call.Operation == nameof(ILanChatCore.Dispatch)));
        Assert.AreEqual(1, fake.DisposeCallCount);
    }

    [TestMethod]
    public async Task Active_runtime_polls_while_dispatch_queue_remains_nonempty()
    {
        using var firstDispatchGate = new ManualResetEventSlim(false);
        var fake = new FakeLanChatCore
        {
            DispatchDelay = TimeSpan.FromMilliseconds(10),
            DispatchGate = firstDispatchGate,
        };
        await using var runtime = new CoreRuntime(() => fake, TimeSpan.FromMilliseconds(20));
        var snapshots = new System.Collections.Concurrent.ConcurrentQueue<Core.Models.CoreSnapshot>();
        runtime.StateChanged += (_, snapshot) => snapshots.Enqueue(snapshot);
        await runtime.StartAsync().WaitAsync(Timeout);
        runtime.SetActive(true);
        await WaitUntilAsync(() => fake.DrainEventsCallCount > 0);

        var firstCommand = runtime.DispatchAsync("blocked-first");
        await WaitUntilAsync(() => fake.Calls.Any(call =>
            call.Operation == nameof(ILanChatCore.Dispatch) && call.Value == "blocked-first"));
        fake.EnqueueEvent("{\"kind\":\"state\"}");
        fake.EnqueueState("{\"schemaVersion\":1,\"connection\":{\"phase\":\"connected\"}}");
        var commands = Enumerable.Range(0, 119)
            .Select(index => runtime.DispatchAsync($"queued-{index}"))
            .Prepend(firstCommand)
            .ToArray();
        firstDispatchGate.Set();

        await WaitUntilAsync(() => snapshots.Count == 2, TimeSpan.FromMilliseconds(500));
        Assert.IsTrue(commands.Any(command => !command.IsCompleted));
        await Task.WhenAll(commands).WaitAsync(Timeout);
        Assert.AreEqual("connected", snapshots.Last().ConnectionPhase);
    }

    [TestMethod]
    public async Task State_changed_subscriber_failure_reports_error_and_core_thread_continues()
    {
        var failure = new InvalidOperationException("subscriber failed");
        var fake = new FakeLanChatCore { DispatchResult = 29 };
        await using var runtime = new CoreRuntime(() => fake, TimeSpan.FromMilliseconds(20));
        var handlerCalls = 0;
        var successfulPublications = 0;
        var reported = new TaskCompletionSource<Exception>(TaskCreationOptions.RunContinuationsAsynchronously);
        runtime.StateChanged += (_, _) =>
        {
            if (Interlocked.Increment(ref handlerCalls) == 1)
            {
                throw failure;
            }
        };
        runtime.StateChanged += (_, _) => Interlocked.Increment(ref successfulPublications);
        runtime.RuntimeError += (_, error) => reported.TrySetResult(error);

        await runtime.StartAsync().WaitAsync(Timeout);
        Assert.AreSame(failure, await reported.Task.WaitAsync(Timeout));
        fake.EnqueueEvent("{\"kind\":\"state\"}");
        fake.EnqueueState("{\"schemaVersion\":1,\"connection\":{\"phase\":\"connected\"}}");
        runtime.SetActive(true);

        await WaitUntilAsync(() => successfulPublications == 2);
        Assert.AreEqual(29, await runtime.DispatchAsync("still-running").WaitAsync(Timeout));
        Assert.AreEqual(0, fake.DisposeCallCount);
    }

    private static Task<int> RecordSynchronousContinuationThread(Task task) => task.ContinueWith(
        _ => Environment.CurrentManagedThreadId,
        CancellationToken.None,
        TaskContinuationOptions.ExecuteSynchronously,
        TaskScheduler.Default);

    private static Task WaitUntilAsync(Func<bool> condition) => WaitUntilAsync(condition, Timeout);

    private static async Task WaitUntilAsync(Func<bool> condition, TimeSpan timeout)
    {
        var started = System.Diagnostics.Stopwatch.GetTimestamp();
        while (!condition() && System.Diagnostics.Stopwatch.GetElapsedTime(started) < timeout)
        {
            await Task.Delay(10);
        }

        Assert.IsTrue(condition(), "等待异步条件超时。");
    }
}
