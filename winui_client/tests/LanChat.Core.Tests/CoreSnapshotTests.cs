using LanChat.Core.Models;

namespace LanChat.Core.Tests;

[TestClass]
public sealed class CoreSnapshotTests
{
    [TestMethod]
    public void Parse_accepts_unknown_fields_and_reads_idle_connection()
    {
        const string json = """
          {"schemaVersion":1,"connection":{"phase":"idle","statusText":"未连接"},"future":{"value":1}}
          """;

        var snapshot = CoreSnapshot.Parse(json);

        Assert.AreEqual(1, snapshot.SchemaVersion);
        Assert.AreEqual("idle", snapshot.ConnectionPhase);
        Assert.AreEqual("未连接", snapshot.StatusText);
    }

    [TestMethod]
    public void Parse_uses_connection_defaults_when_fields_are_missing()
    {
        var snapshot = CoreSnapshot.Parse("{\"schemaVersion\":1,\"connection\":{}}");

        Assert.AreEqual("idle", snapshot.ConnectionPhase);
        Assert.AreEqual("未连接", snapshot.StatusText);
    }

    [TestMethod]
    public void Parse_rejects_unsupported_schema()
    {
        Assert.ThrowsExactly<FormatException>(() =>
            CoreSnapshot.Parse("{\"schemaVersion\":2,\"connection\":{}}"));
        Assert.ThrowsExactly<FormatException>(() => CoreSnapshot.Parse("{\"connection\":{}}"));
    }

    [TestMethod]
    public void Parse_rejects_non_object_root()
    {
        Assert.ThrowsExactly<FormatException>(() => CoreSnapshot.Parse("[]"));
    }

    [TestMethod]
    public void Parse_rejects_missing_or_non_object_connection()
    {
        Assert.ThrowsExactly<FormatException>(() => CoreSnapshot.Parse("{\"schemaVersion\":1}"));
        Assert.ThrowsExactly<FormatException>(() =>
            CoreSnapshot.Parse("{\"schemaVersion\":1,\"connection\":\"idle\"}"));
    }

    [TestMethod]
    public void Parse_normalizes_invalid_json_and_schema_type_errors_to_format_exception()
    {
        Assert.ThrowsExactly<FormatException>(() => CoreSnapshot.Parse("{"));
        Assert.ThrowsExactly<FormatException>(() =>
            CoreSnapshot.Parse("{\"schemaVersion\":\"1\",\"connection\":{}}"));
    }

    [TestMethod]
    public void Parse_normalizes_connection_value_type_errors_to_format_exception()
    {
        Assert.ThrowsExactly<FormatException>(() =>
            CoreSnapshot.Parse("{\"schemaVersion\":1,\"connection\":{\"phase\":1}}"));
        Assert.ThrowsExactly<FormatException>(() =>
            CoreSnapshot.Parse("{\"schemaVersion\":1,\"connection\":{\"statusText\":true}}"));
    }
}
