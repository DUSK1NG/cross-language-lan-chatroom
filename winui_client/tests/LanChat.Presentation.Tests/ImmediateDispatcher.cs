using LanChat.Presentation;

namespace LanChat.Presentation.Tests;

internal sealed class ImmediateDispatcher : IUiDispatcher
{
    public int EnqueueCount { get; private set; }

    public void Enqueue(Action action)
    {
        EnqueueCount++;
        action();
    }
}
