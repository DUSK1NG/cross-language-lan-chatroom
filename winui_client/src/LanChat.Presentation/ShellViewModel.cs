using System.ComponentModel;
using LanChat.Core.Models;
using LanChat.Core.Runtime;

namespace LanChat.Presentation;

public sealed class ShellViewModel : INotifyPropertyChanged, IAsyncDisposable
{
    private readonly ICoreRuntime _runtime;
    private readonly IUiDispatcher _dispatcher;
    private readonly object _lifecycleLock = new();
    private readonly SemaphoreSlim _setActiveGate = new(1, 1);
    [ThreadStatic]
    private static Task? s_executingUiTicket;
    private readonly List<Task> _pendingUiOperations = [];
    private string _connectionPhase = "starting";
    private string _statusText = "正在初始化…";
    private string? _diagnosticMessage;
    private bool _hasFatalError;
    private bool _isDisposed;
    private int _generation;
    private TaskCompletionSource? _startCompletion;
    private Task? _startTask;
    private Task? _disposeTask;

    public ShellViewModel(ICoreRuntime runtime, IUiDispatcher dispatcher)
    {
        _runtime = runtime ?? throw new ArgumentNullException(nameof(runtime));
        _dispatcher = dispatcher ?? throw new ArgumentNullException(nameof(dispatcher));
        _runtime.StateChanged += OnStateChanged;
        _runtime.RuntimeError += OnRuntimeError;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public string ConnectionPhase => _connectionPhase;
    public string StatusText => _statusText;
    public string? DiagnosticMessage => _diagnosticMessage;
    public bool HasFatalError => _hasFatalError;

    public Task StartAsync()
    {
        TaskCompletionSource completion;
        lock (_lifecycleLock)
        {
            ThrowIfDisposedLocked();
            if (_startTask is not null)
            {
                return _startTask;
            }

            completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            _startCompletion = completion;
            _startTask = completion.Task;
        }

        _ = Task.Run(() => StartRuntimeAsync(completion));
        return completion.Task;
    }

    public void SetActive(bool active)
    {
        ThrowIfDisposed();
        _setActiveGate.Wait();
        try
        {
            ThrowIfDisposed();
            _runtime.SetActive(active);
        }
        finally
        {
            _setActiveGate.Release();
        }
    }

    public ValueTask DisposeAsync()
    {
        TaskCompletionSource disposeCompletion;
        TaskCompletionSource? cancelledStart;
        Task[] pendingUiOperations;
        var currentUiTicket = s_executingUiTicket;
        lock (_lifecycleLock)
        {
            if (_disposeTask is not null)
            {
                return new ValueTask(_disposeTask);
            }

            _isDisposed = true;
            _generation++;
            disposeCompletion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            _disposeTask = disposeCompletion.Task;
            cancelledStart = _startCompletion;
            pendingUiOperations = [.. _pendingUiOperations.Where(ticket => ticket != currentUiTicket)];
        }

        cancelledStart?.TrySetException(new ObjectDisposedException(nameof(ShellViewModel)));
        _ = DisposeRuntimeAsync(disposeCompletion, Task.WhenAll(pendingUiOperations));
        return new ValueTask(disposeCompletion.Task);
    }

    private async Task StartRuntimeAsync(TaskCompletionSource completion)
    {
        lock (_lifecycleLock)
        {
            if (_isDisposed || completion.Task.IsCompleted)
            {
                return;
            }
        }

        Exception? startupException = null;
        try
        {
            await _runtime.StartAsync().ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            startupException = exception;
        }

        if (!IsDisposed() && startupException is not null)
        {
            try
            {
                await EnqueueIfActive(generation => ApplyStartupFailure(startupException, generation))
                    .ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                completion.TrySetException(exception);
                return;
            }
        }

        if (IsDisposed())
        {
            completion.TrySetException(new ObjectDisposedException(nameof(ShellViewModel)));
            return;
        }

        completion.TrySetResult();
    }

    private async Task DisposeRuntimeAsync(TaskCompletionSource completion, Task uiBarrier)
    {
        Exception? failure = null;
        try
        {
            _runtime.StateChanged -= OnStateChanged;
            _runtime.RuntimeError -= OnRuntimeError;
            await _setActiveGate.WaitAsync().ConfigureAwait(false);
            try
            {
                await _runtime.DisposeAsync().ConfigureAwait(false);
            }
            finally
            {
                _setActiveGate.Release();
            }
        }
        catch (Exception exception)
        {
            failure = exception;
        }

        try
        {
            await uiBarrier.ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            failure ??= exception;
        }

        if (failure is null)
        {
            completion.TrySetResult();
        }
        else
        {
            completion.TrySetException(failure);
        }
    }

    private void OnStateChanged(object? sender, CoreSnapshot snapshot) =>
        ObserveUiOperation(EnqueueIfActive(generation => ApplySnapshot(snapshot, generation)));

    private void OnRuntimeError(object? sender, Exception exception) =>
        ObserveUiOperation(EnqueueIfActive(generation => ApplyRuntimeError(exception, generation)));

    private Task EnqueueIfActive(Action<int> action)
    {
        TaskCompletionSource ticket;
        int generation;
        lock (_lifecycleLock)
        {
            if (_isDisposed)
            {
                return Task.CompletedTask;
            }

            generation = _generation;
            ticket = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            _pendingUiOperations.Add(ticket.Task);
        }

        try
        {
            var dispatchTask = _dispatcher.EnqueueAsync(() => ExecuteUiAction(ticket.Task, action, generation));
            ArgumentNullException.ThrowIfNull(dispatchTask);
            _ = CompleteUiTicketAsync(ticket, dispatchTask);
        }
        catch (Exception exception)
        {
            ticket.TrySetException(exception);
            RemoveUiTicket(ticket.Task);
        }

        return ticket.Task;
    }

    private async Task CompleteUiTicketAsync(TaskCompletionSource ticket, Task dispatchTask)
    {
        try
        {
            await dispatchTask.ConfigureAwait(false);
            ticket.TrySetResult();
        }
        catch (Exception exception)
        {
            ticket.TrySetException(exception);
        }
        finally
        {
            RemoveUiTicket(ticket.Task);
        }
    }

    private void ObserveUiOperation(Task operation) => _ = ObserveUiOperationAsync(operation);

    private async Task ObserveUiOperationAsync(Task operation)
    {
        try
        {
            await operation.ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            FailStart(exception);
        }
    }

    private void FailStart(Exception exception)
    {
        TaskCompletionSource? completion;
        lock (_lifecycleLock)
        {
            completion = _startCompletion;
        }

        completion?.TrySetException(exception);
    }

    private void RemoveUiTicket(Task ticket)
    {
        lock (_lifecycleLock)
        {
            _pendingUiOperations.Remove(ticket);
        }
    }

    private void ExecuteUiAction(Task ticket, Action<int> action, int generation)
    {
        var previousTicket = s_executingUiTicket;
        s_executingUiTicket = ticket;
        try
        {
            action(generation);
        }
        finally
        {
            s_executingUiTicket = previousTicket;
        }
    }

    private void ApplySnapshot(CoreSnapshot snapshot, int generation)
    {
        TrySetProperty(ref _connectionPhase, snapshot.ConnectionPhase, nameof(ConnectionPhase), generation);
        TrySetProperty(ref _statusText, snapshot.StatusText, nameof(StatusText), generation);
    }

    private void ApplyStartupFailure(Exception exception, int generation)
    {
        TrySetProperty(ref _hasFatalError, true, nameof(HasFatalError), generation);
        TrySetProperty(ref _diagnosticMessage, FormatDiagnostic(exception), nameof(DiagnosticMessage), generation);
    }

    private void ApplyRuntimeError(Exception exception, int generation) =>
        TrySetProperty(ref _diagnosticMessage, FormatDiagnostic(exception), nameof(DiagnosticMessage), generation);

    private bool TrySetProperty<T>(ref T field, T value, string propertyName, int generation)
    {
        PropertyChangedEventHandler? handler;
        lock (_lifecycleLock)
        {
            if (_isDisposed || _generation != generation || EqualityComparer<T>.Default.Equals(field, value))
            {
                return false;
            }

            field = value;
            handler = PropertyChanged;
        }

        handler?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        return true;
    }

    private bool IsDisposed()
    {
        lock (_lifecycleLock)
        {
            return _isDisposed;
        }
    }

    private void ThrowIfDisposed()
    {
        lock (_lifecycleLock)
        {
            ThrowIfDisposedLocked();
        }
    }

    private void ThrowIfDisposedLocked()
    {
        if (_isDisposed)
        {
            throw new ObjectDisposedException(nameof(ShellViewModel));
        }
    }

    private static string FormatDiagnostic(Exception exception)
    {
        var detail = exception.ToString();
        if (exception is FileNotFoundException { FileName: { Length: > 0 } fileName } &&
            !detail.Contains(fileName, StringComparison.Ordinal))
        {
            detail = $"{detail}{Environment.NewLine}文件路径: {fileName}";
        }

        return detail;
    }
}
