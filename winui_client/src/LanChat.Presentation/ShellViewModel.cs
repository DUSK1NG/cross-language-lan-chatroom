using System.ComponentModel;
using System.Runtime.CompilerServices;
using LanChat.Core.Models;
using LanChat.Core.Runtime;

namespace LanChat.Presentation;

public sealed class ShellViewModel : INotifyPropertyChanged, IAsyncDisposable
{
    private readonly ICoreRuntime _runtime;
    private readonly IUiDispatcher _dispatcher;
    private readonly object _lifecycleLock = new();
    private string _connectionPhase = "starting";
    private string _statusText = "正在初始化…";
    private string? _diagnosticMessage;
    private bool _hasFatalError;
    private bool _isDisposed;
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

    public string ConnectionPhase
    {
        get => _connectionPhase;
        private set => SetProperty(ref _connectionPhase, value);
    }

    public string StatusText
    {
        get => _statusText;
        private set => SetProperty(ref _statusText, value);
    }

    public string? DiagnosticMessage
    {
        get => _diagnosticMessage;
        private set => SetProperty(ref _diagnosticMessage, value);
    }

    public bool HasFatalError
    {
        get => _hasFatalError;
        private set => SetProperty(ref _hasFatalError, value);
    }

    public Task StartAsync()
    {
        lock (_lifecycleLock)
        {
            ThrowIfDisposed();
            return _startTask ??= StartRuntimeAsync();
        }
    }

    public void SetActive(bool active)
    {
        lock (_lifecycleLock)
        {
            ThrowIfDisposed();
            _runtime.SetActive(active);
        }
    }

    public ValueTask DisposeAsync()
    {
        lock (_lifecycleLock)
        {
            if (_disposeTask is not null)
            {
                return new ValueTask(_disposeTask);
            }

            _isDisposed = true;
            _runtime.StateChanged -= OnStateChanged;
            _runtime.RuntimeError -= OnRuntimeError;
            try
            {
                _disposeTask = _runtime.DisposeAsync().AsTask();
            }
            catch (Exception exception)
            {
                _disposeTask = Task.FromException(exception);
            }

            return new ValueTask(_disposeTask);
        }
    }

    private async Task StartRuntimeAsync()
    {
        try
        {
            await _runtime.StartAsync().ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            EnqueueIfActive(() => ApplyStartupFailure(exception));
        }

        ThrowIfDisposed();
    }

    private void OnStateChanged(object? sender, CoreSnapshot snapshot) =>
        EnqueueIfActive(() => ApplySnapshot(snapshot));

    private void OnRuntimeError(object? sender, Exception exception) =>
        EnqueueIfActive(() => DiagnosticMessage = FormatDiagnostic(exception));

    private void EnqueueIfActive(Action action)
    {
        lock (_lifecycleLock)
        {
            if (_isDisposed)
            {
                return;
            }

            _dispatcher.Enqueue(() =>
            {
                lock (_lifecycleLock)
                {
                    if (_isDisposed)
                    {
                        return;
                    }

                    action();
                }
            });
        }
    }

    private void ApplySnapshot(CoreSnapshot snapshot)
    {
        ConnectionPhase = snapshot.ConnectionPhase;
        if (_isDisposed)
        {
            return;
        }

        StatusText = snapshot.StatusText;
    }

    private void ApplyStartupFailure(Exception exception)
    {
        HasFatalError = true;
        if (_isDisposed)
        {
            return;
        }

        DiagnosticMessage = FormatDiagnostic(exception);
    }

    private void ThrowIfDisposed()
    {
        lock (_lifecycleLock)
        {
            if (_isDisposed)
            {
                throw new ObjectDisposedException(nameof(ShellViewModel));
            }
        }
    }

    private bool SetProperty<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        return true;
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
