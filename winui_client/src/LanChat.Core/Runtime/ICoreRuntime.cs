using LanChat.Core.Models;

namespace LanChat.Core.Runtime;

public interface ICoreRuntime : IAsyncDisposable
{
    event EventHandler<CoreSnapshot>? StateChanged;
    event EventHandler<Exception>? RuntimeError;
    Task StartAsync();
    Task<int> DispatchAsync(string commandJson);
    void SetActive(bool active);
}
