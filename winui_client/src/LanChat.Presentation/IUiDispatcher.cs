namespace LanChat.Presentation;

public interface IUiDispatcher
{
    Task EnqueueAsync(Action action);
}
