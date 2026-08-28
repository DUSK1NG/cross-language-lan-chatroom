using System.Runtime.InteropServices;
using System.Text;
using LanChat.Core.Interop;

namespace LanChat.Core.Tests;

internal sealed class FakeLanChatCoreNative : ILanChatCoreNative
{
    private readonly Queue<string> _states = new();
    private readonly Queue<string> _events = new();

    public nint CreateResult { get; set; }
    public int DispatchResult { get; set; }
    public int DestroyCallCount { get; private set; }
    public nint DestroyedHandle { get; private set; }
    public int FreeStringCallCount { get; private set; }
    public int TakeEventCallCount { get; private set; }
    public byte[] LastDispatchUtf8 { get; private set; } = [];

    public nint Create() => CreateResult;

    public void Destroy(nint handle)
    {
        DestroyCallCount++;
        DestroyedHandle = handle;
    }

    public int DispatchJson(nint handle, string commandJson)
    {
        LastDispatchUtf8 = Encoding.UTF8.GetBytes(commandJson);
        return DispatchResult;
    }

    public nint CurrentStateJson(nint handle) => TakeString(_states);

    public nint TakeEventJson(nint handle)
    {
        TakeEventCallCount++;
        return TakeString(_events);
    }

    public void FreeString(nint value)
    {
        FreeStringCallCount++;
        Marshal.FreeCoTaskMem(value);
    }

    public void EnqueueState(string value) => _states.Enqueue(value);

    public void EnqueueEvent(string value) => _events.Enqueue(value);

    private static nint TakeString(Queue<string> values) => values.Count == 0
        ? nint.Zero
        : Marshal.StringToCoTaskMemUTF8(values.Dequeue());
}
