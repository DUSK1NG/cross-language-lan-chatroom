using LanChat.Core.Models;

namespace LanChat.Core.Native.Tests;

[TestClass]
public sealed class RealCoreSmokeTests
{
    [TestMethod]
    public void Real_core_reports_idle_disconnected_schema_one_snapshot()
    {
        if (!string.Equals(
                Environment.GetEnvironmentVariable("LAN_CHAT_CORE_NATIVE_TEST"),
                "1",
                StringComparison.Ordinal))
        {
            Assert.Inconclusive("设置 LAN_CHAT_CORE_NATIVE_TEST=1 后运行真实 Core 测试。");
        }

        using var core = LanChatCoreClient.Open(AppContext.BaseDirectory);
        var snapshot = CoreSnapshot.Parse(core.ReadStateJson());

        Assert.AreEqual(1, snapshot.SchemaVersion);
        Assert.AreEqual("idle", snapshot.ConnectionPhase);
        Assert.AreEqual("未连接", snapshot.StatusText);
    }
}
