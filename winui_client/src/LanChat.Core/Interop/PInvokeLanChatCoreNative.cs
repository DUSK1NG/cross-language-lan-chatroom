using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace LanChat.Core.Interop;

internal sealed partial class PInvokeLanChatCoreNative : ILanChatCoreNative
{
    private const string Library = "lan_chat_core.dll";

    [LibraryImport(Library, EntryPoint = "lan_chat_core_create")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial nint CreateNative();

    [LibraryImport(Library, EntryPoint = "lan_chat_core_destroy")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial void DestroyNative(nint handle);

    [LibraryImport(Library, EntryPoint = "lan_chat_core_dispatch_json", StringMarshalling = StringMarshalling.Utf8)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial int DispatchNative(nint handle, string commandJson);

    [LibraryImport(Library, EntryPoint = "lan_chat_core_current_state_json")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial nint CurrentStateNative(nint handle);

    [LibraryImport(Library, EntryPoint = "lan_chat_core_take_event_json")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial nint TakeEventNative(nint handle);

    [LibraryImport(Library, EntryPoint = "lan_chat_core_free_string")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial void FreeStringNative(nint value);

    public nint Create() => CreateNative();
    public void Destroy(nint handle) => DestroyNative(handle);
    public int DispatchJson(nint handle, string commandJson) => DispatchNative(handle, commandJson);
    public nint CurrentStateJson(nint handle) => CurrentStateNative(handle);
    public nint TakeEventJson(nint handle) => TakeEventNative(handle);
    public void FreeString(nint value) => FreeStringNative(value);
}
