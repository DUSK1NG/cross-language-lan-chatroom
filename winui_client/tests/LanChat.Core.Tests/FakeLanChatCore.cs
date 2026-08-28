using System.Collections.Concurrent;

namespace LanChat.Core.Tests;

internal sealed class FakeLanChatCore : ILanChatCore
{
    private readonly ConcurrentQueue<string> _states = new();
    private readonly ConcurrentQueue<string> _events = new();
    private readonly ConcurrentQueue<Call> _calls = new();
    private readonly ConcurrentQueue<Exception> _readStateFailures = new();
    private readonly ConcurrentQueue<Exception> _dispatchFailures = new();
    private readonly ConcurrentQueue<Exception> _drainEventsFailures = new();
    private readonly ConcurrentQueue<Exception> _disposeFailures = new();

    public FakeLanChatCore()
    {
        EnqueueState("{\"schemaVersion\":1,\"connection\":{\"phase\":\"idle\",\"statusText\":\"未连接\"}}");
    }

    public IReadOnlyList<Call> Calls => _calls.ToArray();
    public IReadOnlyList<int> CallThreadIds => Calls.Select(call => call.ThreadId).ToArray();
    public int DispatchResult { get; set; }
    public ManualResetEventSlim? ReadStateGate { get; set; }
    public ManualResetEventSlim? DispatchGate { get; set; }
    public ManualResetEventSlim? DisposeGate { get; set; }
    public int DisposeCallCount => Calls.Count(call => call.Operation == nameof(Dispose));
    public int DrainEventsCallCount => Calls.Count(call => call.Operation == nameof(DrainEvents));
    public int ReadStateCallCount => Calls.Count(call => call.Operation == nameof(ReadStateJson));

    public string ReadStateJson()
    {
        RecordCall(nameof(ReadStateJson));
        WaitForGate(ReadStateGate);
        if (_readStateFailures.TryDequeue(out var failure))
        {
            throw failure;
        }

        return _states.TryDequeue(out var state)
            ? state
            : throw new InvalidOperationException("测试未提供状态快照。");
    }

    public int Dispatch(string commandJson)
    {
        RecordCall(nameof(Dispatch), commandJson);
        WaitForGate(DispatchGate);
        if (_dispatchFailures.TryDequeue(out var failure))
        {
            throw failure;
        }

        return DispatchResult;
    }

    public IReadOnlyList<string> DrainEvents()
    {
        RecordCall(nameof(DrainEvents));
        if (_drainEventsFailures.TryDequeue(out var failure))
        {
            throw failure;
        }

        var events = new List<string>();
        while (_events.TryDequeue(out var value))
        {
            events.Add(value);
        }

        return events;
    }

    public void Dispose()
    {
        RecordCall(nameof(Dispose));
        WaitForGate(DisposeGate);
        if (_disposeFailures.TryDequeue(out var failure))
        {
            throw failure;
        }
    }

    public void EnqueueState(string value) => _states.Enqueue(value);
    public void EnqueueEvent(string value) => _events.Enqueue(value);
    public void FailNextReadState(Exception exception) => _readStateFailures.Enqueue(exception);
    public void FailNextDispatch(Exception exception) => _dispatchFailures.Enqueue(exception);
    public void FailNextDrainEvents(Exception exception) => _drainEventsFailures.Enqueue(exception);
    public void FailNextDispose(Exception exception) => _disposeFailures.Enqueue(exception);

    private void RecordCall(string operation, string? value = null) => _calls.Enqueue(
        new Call(operation, Environment.CurrentManagedThreadId, Thread.CurrentThread.Name, value));

    private static void WaitForGate(ManualResetEventSlim? gate)
    {
        if (gate is not null && !gate.Wait(TimeSpan.FromSeconds(3)))
        {
            throw new TimeoutException("测试门限等待超时。");
        }
    }

    internal sealed record Call(string Operation, int ThreadId, string? ThreadName, string? Value);
}
