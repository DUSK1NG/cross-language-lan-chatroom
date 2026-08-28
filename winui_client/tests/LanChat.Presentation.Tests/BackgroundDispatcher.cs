using LanChat.Presentation;

namespace LanChat.Presentation.Tests;

internal sealed class BackgroundDispatcher : IUiDispatcher
{
    public Task EnqueueAsync(Action action) => Task.Run(action);
}
