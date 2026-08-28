using LanChat.Core.Models;
using LanChat.Core.Runtime;

namespace LanChat.Presentation.Tests;

internal sealed class FakeCoreRuntime(
    Exception? startException = null,
    Exception? disposeException = null,
    bool throwDuringDispose = false,
    bool throwDuringStart = false,
    bool delayStart = false,
    bool blockStartSynchronously = false) : ICoreRuntime
{
    private readonly Exception? _startException = startException;
    private readonly Exception? _disposeException = disposeException;
    private readonly bool _throwDuringDispose = throwDuringDispose;
    private readonly bool _throwDuringStart = throwDuringStart;
    private readonly TaskCompletionSource? _startCompletion = delayStart
        ? new(TaskCreationOptions.RunContinuationsAsynchronously)
        : null;
    private readonly ManualResetEventSlim _startEntered = new();
    private readonly ManualResetEventSlim? _blockedStartEntered = blockStartSynchronously ? new() : null;
    private readonly ManualResetEventSlim? _blockedStartReleased = blockStartSynchronously ? new() : null;
    private EventHandler<CoreSnapshot>? _stateChanged;
    private EventHandler<Exception>? _runtimeError;

    public event EventHandler<CoreSnapshot>? StateChanged
    {
        add => _stateChanged += value;
        remove => _stateChanged -= value;
    }

    public event EventHandler<Exception>? RuntimeError
    {
        add => _runtimeError += value;
        remove => _runtimeError -= value;
    }

    public int DisposeCount { get; private set; }
    public int StartCount { get; private set; }
    public bool? LastSetActive { get; private set; }
    public int StateChangedSubscriberCount => _stateChanged?.GetInvocationList().Length ?? 0;
    public int RuntimeErrorSubscriberCount => _runtimeError?.GetInvocationList().Length ?? 0;

    public bool WaitForBlockedStart(TimeSpan timeout) => _blockedStartEntered?.Wait(timeout) ?? false;

    public bool WaitForStart(TimeSpan timeout) => _startEntered.Wait(timeout);

    public void ReleaseBlockedStart() => _blockedStartReleased?.Set();

    public Task StartAsync()
    {
        StartCount++;
        _startEntered.Set();
        if (_blockedStartEntered is not null && _blockedStartReleased is not null)
        {
            _blockedStartEntered.Set();
            _blockedStartReleased.Wait();
        }

        if (_startException is not null)
        {
            if (_throwDuringStart)
            {
                throw _startException;
            }

            return Task.FromException(_startException);
        }

        return _startCompletion?.Task ?? Task.CompletedTask;
    }

    public Task<int> DispatchAsync(string commandJson) => Task.FromResult(0);

    public void SetActive(bool active) => LastSetActive = active;

    public ValueTask DisposeAsync()
    {
        DisposeCount++;
        ReleaseBlockedStart();
        _startCompletion?.TrySetException(new ObjectDisposedException(nameof(FakeCoreRuntime)));
        if (_disposeException is not null)
        {
            if (_throwDuringDispose)
            {
                throw _disposeException;
            }

            return ValueTask.FromException(_disposeException);
        }

        return ValueTask.CompletedTask;
    }

    public void PublishState(CoreSnapshot snapshot) => _stateChanged?.Invoke(this, snapshot);

    public void PublishError(Exception exception) => _runtimeError?.Invoke(this, exception);
}
