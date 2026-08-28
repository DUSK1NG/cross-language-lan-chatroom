using LanChat.Core.Models;

namespace LanChat.Core.Tests;

[TestClass]
public sealed class CoreEventTests
{
    [TestMethod]
    public void Parse_reads_kind_and_state_requires_snapshot_refresh()
    {
        var coreEvent = CoreEvent.Parse("{\"kind\":\"state\"}");

        Assert.AreEqual("state", coreEvent.Kind);
        Assert.IsTrue(coreEvent.RequiresSnapshotRefresh);
    }

    [TestMethod]
    public void Parse_supports_legacy_type_field()
    {
        var coreEvent = CoreEvent.Parse("{\"type\":\"result\"}");

        Assert.AreEqual("result", coreEvent.Kind);
        Assert.IsFalse(coreEvent.RequiresSnapshotRefresh);
    }

    [TestMethod]
    public void Parse_treats_unknown_events_as_not_requiring_snapshot_refresh()
    {
        var coreEvent = CoreEvent.Parse("{\"kind\":\"future\",\"value\":1}");

        Assert.IsFalse(coreEvent.RequiresSnapshotRefresh);
    }

    [TestMethod]
    public void Parse_rejects_non_object_json()
    {
        Assert.ThrowsExactly<FormatException>(() => CoreEvent.Parse("[]"));
    }

    [TestMethod]
    public void Parse_normalizes_invalid_json_to_format_exception()
    {
        Assert.ThrowsExactly<FormatException>(() => CoreEvent.Parse("{"));
    }

    [TestMethod]
    public void Parse_normalizes_kind_value_type_errors_to_format_exception()
    {
        Assert.ThrowsExactly<FormatException>(() => CoreEvent.Parse("{\"kind\":1}"));
        Assert.ThrowsExactly<FormatException>(() => CoreEvent.Parse("{\"type\":true}"));
    }
}
