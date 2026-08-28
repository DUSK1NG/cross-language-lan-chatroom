using LanChat.Presentation;
using Microsoft.UI.Dispatching;

namespace LanChat_WinUI;

public sealed class DispatcherQueueAdapter : IUiDispatcher
{
    private readonly DispatcherQueue _dispatcherQueue;

    public DispatcherQueueAdapter(DispatcherQueue dispatcherQueue)
    {
        _dispatcherQueue = dispatcherQueue ?? throw new ArgumentNullException(nameof(dispatcherQueue));
    }

    public Task EnqueueAsync(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);

        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        if (!_dispatcherQueue.TryEnqueue(() => Execute(action, completion)))
        {
            completion.TrySetException(new InvalidOperationException("无法调度 WinUI 状态更新。"));
        }

        return completion.Task;
    }

    private static void Execute(Action action, TaskCompletionSource completion)
    {
        try
        {
            action();
            completion.TrySetResult();
        }
        catch (Exception exception)
        {
            completion.TrySetException(exception);
        }
    }
}
