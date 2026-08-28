using LanChat.Core.Models;
using LanChat.Core.Runtime;

namespace LanChat.Presentation.Tests;

internal sealed class FakeCoreRuntime(
    Exception? startException = null,
    Exception? disposeException = null,
    bool throwDuringDispose = false) : ICoreRuntime
{
    private readonly Exception? _startException = startException;
    private readonly Exception? _disposeException = disposeException;
    private readonly bool _throwDuringDispose = throwDuringDispose;

    public event EventHandler<CoreSnapshot>? StateChanged;
    public event EventHandler<Exception>? RuntimeError;

    public int DisposeCount { get; private set; }
    public bool? LastSetActive { get; private set; }

    public Task StartAsync() => _startException is null
        ? Task.CompletedTask
        : Task.FromException(_startException);

    public Task<int> DispatchAsync(string commandJson) => Task.FromResult(0);

    public void SetActive(bool active) => LastSetActive = active;

    public ValueTask DisposeAsync()
    {
        DisposeCount++;
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

    public void PublishState(CoreSnapshot snapshot) => StateChanged?.Invoke(this, snapshot);

    public void PublishError(Exception exception) => RuntimeError?.Invoke(this, exception);
}
