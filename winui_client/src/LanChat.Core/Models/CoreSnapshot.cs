using System.Text.Json;

namespace LanChat.Core.Models;

public sealed record CoreSnapshot(int SchemaVersion, string ConnectionPhase, string StatusText)
{
    public static CoreSnapshot Parse(string json)
    {
        try
        {
            using var document = JsonDocument.Parse(json);
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object ||
                !root.TryGetProperty("schemaVersion", out var schema) ||
                schema.ValueKind != JsonValueKind.Number ||
                !schema.TryGetInt32(out var schemaVersion) ||
                schemaVersion != 1)
            {
                throw new FormatException("不支持的 LAN Chat 状态版本。");
            }

            if (!root.TryGetProperty("connection", out var connection) ||
                connection.ValueKind != JsonValueKind.Object)
            {
                throw new FormatException("LAN Chat 状态缺少 connection 对象。");
            }

            return new CoreSnapshot(
                schemaVersion,
                ReadOptionalString(connection, "phase", "idle"),
                ReadOptionalString(connection, "statusText", "未连接"));
        }
        catch (JsonException exception)
        {
            throw new FormatException("LAN Chat 状态不是有效的 JSON。", exception);
        }
    }

    private static string ReadOptionalString(JsonElement objectElement, string propertyName, string defaultValue)
    {
        if (!objectElement.TryGetProperty(propertyName, out var value) || value.ValueKind == JsonValueKind.Null)
        {
            return defaultValue;
        }

        if (value.ValueKind != JsonValueKind.String)
        {
            throw new FormatException($"LAN Chat 状态的 {propertyName} 必须是字符串。");
        }

        return value.GetString() ?? defaultValue;
    }
}
