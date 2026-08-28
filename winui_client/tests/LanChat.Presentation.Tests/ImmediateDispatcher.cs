using LanChat.Presentation;

namespace LanChat.Presentation.Tests;

internal sealed class ImmediateDispatcher : IUiDispatcher
{
    public int EnqueueCount { get; private set; }

    public Task EnqueueAsync(Action action)
    {
        EnqueueCount++;
        try
        {
            action();
            return Task.CompletedTask;
        }
        catch (Exception exception)
        {
            return Task.FromException(exception);
        }
    }
}
