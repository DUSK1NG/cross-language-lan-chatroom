using System.Text.Json;

namespace LanChat.Core.Models;

public sealed record CoreEvent(string Kind)
{
    public bool RequiresSnapshotRefresh => string.Equals(Kind, "state", StringComparison.Ordinal);

    public static CoreEvent Parse(string json)
    {
        try
        {
            using var document = JsonDocument.Parse(json);
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
            {
                throw new FormatException("LAN Chat 事件必须是 JSON 对象。");
            }

            return new CoreEvent(ReadKind(root));
        }
        catch (JsonException exception)
        {
            throw new FormatException("LAN Chat 事件不是有效的 JSON。", exception);
        }
    }

    private static string ReadKind(JsonElement root)
    {
        if (root.TryGetProperty("kind", out var kind))
        {
            return ReadOptionalString(kind, "kind");
        }

        return root.TryGetProperty("type", out var type)
            ? ReadOptionalString(type, "type")
            : string.Empty;
    }

    private static string ReadOptionalString(JsonElement value, string propertyName)
    {
        if (value.ValueKind == JsonValueKind.Null)
        {
            return string.Empty;
        }

        if (value.ValueKind != JsonValueKind.String)
        {
            throw new FormatException($"LAN Chat 事件的 {propertyName} 必须是字符串。");
        }

        return value.GetString() ?? string.Empty;
    }
}
