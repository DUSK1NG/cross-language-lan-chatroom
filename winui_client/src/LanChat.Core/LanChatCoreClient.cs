using System.Runtime.InteropServices;
using LanChat.Core.Interop;

namespace LanChat.Core;

public sealed class LanChatCoreClient : ILanChatCore
{
    private readonly ILanChatCoreNative _native;
    private readonly LanChatCoreHandle _handle;

    internal LanChatCoreClient(ILanChatCoreNative native)
    {
        _native = native ?? throw new ArgumentNullException(nameof(native));
        var handle = _native.Create();
        if (handle == nint.Zero)
        {
            throw new InvalidOperationException("原生核心创建失败。");
        }

        _handle = new LanChatCoreHandle(_native, handle);
    }

    public static ILanChatCore Open(string appDirectory)
    {
        CoreLibraryResolver.Install(appDirectory);
        return new LanChatCoreClient(new PInvokeLanChatCoreNative());
    }

    public string ReadStateJson()
    {
        var value = _native.CurrentStateJson(GetHandle());
        if (value == nint.Zero)
        {
            throw new InvalidOperationException("原生核心未返回状态快照。");
        }

        return DecodeAndFree(value);
    }

    public int Dispatch(string commandJson) => _native.DispatchJson(GetHandle(), commandJson);

    public IReadOnlyList<string> DrainEvents()
    {
        var events = new List<string>();
        while (true)
        {
            var value = _native.TakeEventJson(GetHandle());
            if (value == nint.Zero)
            {
                return events;
            }

            events.Add(DecodeAndFree(value));
        }
    }

    public void Dispose() => _handle.Dispose();

    private nint GetHandle()
    {
        if (_handle.IsClosed || _handle.IsInvalid)
        {
            throw new ObjectDisposedException(nameof(LanChatCoreClient));
        }

        return _handle.DangerousGetHandle();
    }

    private string DecodeAndFree(nint value)
    {
        try
        {
            return Marshal.PtrToStringUTF8(value)
                ?? throw new InvalidOperationException("原生核心返回了无效字符串。");
        }
        finally
        {
            _native.FreeString(value);
        }
    }
}
