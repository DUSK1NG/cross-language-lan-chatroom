using LanChat.Presentation;

namespace LanChat.Presentation.Tests;

internal sealed class DeferredDispatcher : IUiDispatcher
{
    private readonly Queue<(Action Action, TaskCompletionSource Completion)> _actions = new();

    public int PendingCount => _actions.Count;

    public Task EnqueueAsync(Action action)
    {
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _actions.Enqueue((action, completion));
        return completion.Task;
    }

    public void Drain()
    {
        while (_actions.TryDequeue(out var item))
        {
            try
            {
                item.Action();
                item.Completion.TrySetResult();
            }
            catch (Exception exception)
            {
                item.Completion.TrySetException(exception);
            }
        }
    }
}
