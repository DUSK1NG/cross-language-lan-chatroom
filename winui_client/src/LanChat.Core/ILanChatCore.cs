namespace LanChat.Core;

public interface ILanChatCore : IDisposable
{
    string ReadStateJson();
    int Dispatch(string commandJson);
    IReadOnlyList<string> DrainEvents();
}
