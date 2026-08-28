namespace LanChat.Core.Tests;

[TestClass]
public sealed class LanChatCoreClientTests
{
    [TestMethod]
    public void Constructor_throws_when_native_create_returns_null_handle()
    {
        var native = new FakeLanChatCoreNative();

        Assert.ThrowsExactly<InvalidOperationException>(() => new LanChatCoreClient(native));
    }

    [TestMethod]
    public void Dispose_releases_handle_once()
    {
        var native = new FakeLanChatCoreNative { CreateResult = (nint)42 };
        var client = new LanChatCoreClient(native);

        client.Dispose();
        client.Dispose();

        Assert.AreEqual(1, native.DestroyCallCount);
        Assert.AreEqual((nint)42, native.DestroyedHandle);
    }

    [TestMethod]
    public void ReadStateJson_decodes_utf8_and_frees_native_string()
    {
        var native = new FakeLanChatCoreNative { CreateResult = (nint)42 };
        native.EnqueueState("{\"status\":\"未连接\"}");
        using var client = new LanChatCoreClient(native);

        Assert.AreEqual("{\"status\":\"未连接\"}", client.ReadStateJson());
        Assert.AreEqual(1, native.FreeStringCallCount);
    }

    [TestMethod]
    public void ReadStateJson_throws_when_native_returns_null()
    {
        using var client = new LanChatCoreClient(new FakeLanChatCoreNative { CreateResult = (nint)42 });

        var exception = Assert.ThrowsExactly<InvalidOperationException>(() => client.ReadStateJson());

        Assert.AreEqual("原生核心未返回状态快照。", exception.Message);
    }

    [TestMethod]
    public void DrainEvents_stops_at_null_and_frees_each_event()
    {
        var native = new FakeLanChatCoreNative { CreateResult = (nint)42 };
        native.EnqueueEvent("{\"kind\":\"state\"}");
        native.EnqueueEvent("{\"kind\":\"result\",\"text\":\"完成\"}");
        using var client = new LanChatCoreClient(native);

        var events = client.DrainEvents();

        CollectionAssert.AreEqual(
            new[] { "{\"kind\":\"state\"}", "{\"kind\":\"result\",\"text\":\"完成\"}" },
            events.ToArray());
        Assert.AreEqual(2, native.FreeStringCallCount);
        Assert.AreEqual(3, native.TakeEventCallCount);
    }

    [TestMethod]
    public void Dispatch_preserves_chinese_command_as_utf8()
    {
        var native = new FakeLanChatCoreNative { CreateResult = (nint)42, DispatchResult = 7 };
        using var client = new LanChatCoreClient(native);

        var result = client.Dispatch("{\"action\":\"发送\",\"text\":\"你好，世界\"}");

        Assert.AreEqual(7, result);
        CollectionAssert.AreEqual(
            System.Text.Encoding.UTF8.GetBytes("{\"action\":\"发送\",\"text\":\"你好，世界\"}"),
            native.LastDispatchUtf8);
    }
}
