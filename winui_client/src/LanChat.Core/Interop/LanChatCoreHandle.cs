using Microsoft.Win32.SafeHandles;

namespace LanChat.Core.Interop;

internal sealed class LanChatCoreHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    private readonly ILanChatCoreNative _native;

    internal LanChatCoreHandle(ILanChatCoreNative native, nint handle)
        : base(ownsHandle: true)
    {
        _native = native;
        SetHandle(handle);
    }

    protected override bool ReleaseHandle()
    {
        _native.Destroy(handle);
        return true;
    }
}
