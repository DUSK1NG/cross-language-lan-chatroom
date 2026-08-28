using System.Collections.Concurrent;
using LanChat.Core.Models;

namespace LanChat.Core.Runtime;

public sealed class CoreRuntime : ICoreRuntime
{
    private readonly Func<ILanChatCore> _coreFactory;
    private readonly int _pollIntervalMilliseconds;
    private readonly BlockingCollection<Action<ILanChatCore>> _workItems = new();
    private readonly CancellationTokenSource _cancellation = new();
    private readonly TaskCompletionSource _started = NewCompletionSource();
    private readonly TaskCompletionSource _disposed = NewCompletionSource();
    private readonly Thread _thread;
    private readonly object _lifecycleLock = new();
    private bool _startRequested;
    private bool _disposeRequested;
    private bool _threadFinished;
    private bool _isActive;

    public CoreRuntime(Func<ILanChatCore> coreFactory, TimeSpan? pollInterval = null)
    {
        _coreFactory = coreFactory ?? throw new ArgumentNullException(nameof(coreFactory));
        var effectivePollInterval = pollInterval ?? TimeSpan.FromMilliseconds(100);
        if (effectivePollInterval <= TimeSpan.Zero || effectivePollInterval.TotalMilliseconds > int.MaxValue)
        {
            throw new ArgumentOutOfRangeException(nameof(pollInterval));
        }

        _pollIntervalMilliseconds = Math.Max(1, (int)Math.Ceiling(effectivePollInterval.TotalMilliseconds));
        _thread = new Thread(Run)
        {
            IsBackground = true,
            Name = "LAN Chat Core",
        };
    }

    public event EventHandler<CoreSnapshot>? StateChanged;
    public event EventHandler<Exception>? RuntimeError;

    public Task StartAsync()
    {
        lock (_lifecycleLock)
        {
            ObjectDisposedException.ThrowIf(_disposeRequested, this);
            if (!_startRequested)
            {
                _startRequested = true;
                try
                {
                    _thread.Start();
                }
                catch (Exception exception)
                {
                    _threadFinished = true;
                    _started.TrySetException(exception);
                    _disposed.TrySetResult();
                    _cancellation.Cancel();
                    DisposeResources();
                }
            }
        }

        return _started.Task;
    }

    public Task<int> DispatchAsync(string commandJson)
    {
        ArgumentNullException.ThrowIfNull(commandJson);
        lock (_lifecycleLock)
        {
            EnsureRunning();
            var completion = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
            _workItems.Add(core =>
            {
                try
                {
                    completion.TrySetResult(core.Dispatch(commandJson));
                }
                catch (Exception exception)
                {
                    completion.TrySetException(exception);
                    PublishError(exception);
                }
            });
            return completion.Task;
        }
    }

    public void SetActive(bool active)
    {
        lock (_lifecycleLock)
        {
            EnsureRunning();
            _workItems.Add(core =>
            {
                var wasActive = _isActive;
                _isActive = active;
                if (active && !wasActive)
                {
                    DrainEvents(core);
                }
            });
        }
    }

    public ValueTask DisposeAsync()
    {
        lock (_lifecycleLock)
        {
            if (_disposeRequested)
            {
                return new ValueTask(_disposed.Task);
            }

            _disposeRequested = true;
            if (!_startRequested)
            {
                _threadFinished = true;
                _cancellation.Cancel();
                DisposeResources();
                _disposed.TrySetResult();
            }
            else if (!_threadFinished)
            {
                _workItems.Add(_ => _cancellation.Cancel());
                _workItems.CompleteAdding();
            }
        }

        return new ValueTask(_disposed.Task);
    }

    private void Run()
    {
        ILanChatCore? core = null;
        Exception? disposalFailure = null;
        try
        {
            core = _coreFactory() ?? throw new InvalidOperationException("核心工厂返回了 null。");
            PublishState(CoreSnapshot.Parse(core.ReadStateJson()));
            _started.TrySetResult();
            RunLoop(core);
        }
        catch (Exception exception)
        {
            _started.TrySetException(exception);
            PublishError(exception);
        }
        finally
        {
            if (core is not null)
            {
                try
                {
                    core.Dispose();
                }
                catch (Exception exception)
                {
                    disposalFailure = exception;
                    PublishError(exception);
                }
            }

            lock (_lifecycleLock)
            {
                _threadFinished = true;
            }

            _cancellation.Cancel();
            DisposeResources();
            if (disposalFailure is null)
            {
                _disposed.TrySetResult();
            }
            else
            {
                _disposed.TrySetException(disposalFailure);
            }
        }
    }

    private void RunLoop(ILanChatCore core)
    {
        while (!_workItems.IsCompleted)
        {
            try
            {
                if (_workItems.TryTake(
                    out var workItem,
                    _isActive ? _pollIntervalMilliseconds : Timeout.Infinite,
                    _cancellation.Token))
                {
                    workItem(core);
                }
                else if (_isActive && !_workItems.IsCompleted)
                {
                    DrainEvents(core);
                }
            }
            catch (OperationCanceledException) when (_cancellation.IsCancellationRequested)
            {
                return;
            }
            catch (Exception exception)
            {
                PublishError(exception);
            }
        }
    }

    private void DrainEvents(ILanChatCore core)
    {
        IReadOnlyList<string> events;
        try
        {
            events = core.DrainEvents();
        }
        catch (Exception exception)
        {
            PublishError(exception);
            return;
        }

        var refreshSnapshot = false;
        foreach (var eventJson in events)
        {
            try
            {
                refreshSnapshot |= CoreEvent.Parse(eventJson).RequiresSnapshotRefresh;
            }
            catch (Exception exception)
            {
                PublishError(exception);
            }
        }

        if (!refreshSnapshot)
        {
            return;
        }

        try
        {
            PublishState(CoreSnapshot.Parse(core.ReadStateJson()));
        }
        catch (Exception exception)
        {
            PublishError(exception);
        }
    }

    private void PublishState(CoreSnapshot snapshot)
    {
        foreach (EventHandler<CoreSnapshot> handler in StateChanged?.GetInvocationList() ?? [])
        {
            try
            {
                handler(this, snapshot);
            }
            catch (Exception exception)
            {
                PublishError(exception);
            }
        }
    }

    private void PublishError(Exception exception)
    {
        foreach (EventHandler<Exception> handler in RuntimeError?.GetInvocationList() ?? [])
        {
            try
            {
                handler(this, exception);
            }
            catch
            {
                // 错误观察者不能终止核心线程。
            }
        }
    }

    private void EnsureRunning()
    {
        ObjectDisposedException.ThrowIf(_disposeRequested, this);
        if (!_startRequested || !_started.Task.IsCompletedSuccessfully || _threadFinished)
        {
            throw new InvalidOperationException("CoreRuntime 尚未成功启动。");
        }
    }

    private void DisposeResources()
    {
        _cancellation.Dispose();
        _workItems.Dispose();
    }

    private static TaskCompletionSource NewCompletionSource() =>
        new(TaskCreationOptions.RunContinuationsAsynchronously);
}
