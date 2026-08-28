using LanChat.Presentation;

namespace LanChat.Presentation.Tests;

internal sealed class ThrowingDispatcher : IUiDispatcher
{
    public Task EnqueueAsync(Action action) => throw new InvalidOperationException("dispatcher enqueue 失败");
}
