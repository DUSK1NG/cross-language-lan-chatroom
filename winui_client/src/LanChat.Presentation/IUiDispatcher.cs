namespace LanChat.Presentation;

public interface IUiDispatcher
{
    void Enqueue(Action action);
}
