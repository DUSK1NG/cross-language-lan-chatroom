using LanChat.Presentation;

namespace LanChat.Presentation.Tests;

internal sealed class DeferredDispatcher : IUiDispatcher
{
    private readonly Queue<Action> _actions = new();

    public int PendingCount => _actions.Count;

    public void Enqueue(Action action) => _actions.Enqueue(action);

    public void Drain()
    {
        while (_actions.TryDequeue(out var action))
        {
            action();
        }
    }
}
