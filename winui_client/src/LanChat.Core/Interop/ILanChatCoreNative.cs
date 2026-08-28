namespace LanChat.Core.Interop;

internal interface ILanChatCoreNative
{
    nint Create();
    void Destroy(nint handle);
    int DispatchJson(nint handle, string commandJson);
    nint CurrentStateJson(nint handle);
    nint TakeEventJson(nint handle);
    void FreeString(nint value);
}
