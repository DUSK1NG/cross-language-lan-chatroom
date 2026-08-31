#include "bridge_protocol.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>

#include <cmath>

namespace bridge {
namespace {

void setInvalid(QString* errorCode) {
    if (errorCode) {
        *errorCode = QStringLiteral("invalid_command");
    }
}

bool nonEmptyString(const QJsonObject& object, const char* key) {
    const QJsonValue value = object.value(QLatin1String(key));
    return value.isString() && !value.toString().trimmed().isEmpty();
}

bool booleanValue(const QJsonObject& object, const char* key) {
    return object.value(QLatin1String(key)).isBool();
}

bool integerValue(const QJsonObject& object, const char* key) {
    const QJsonValue value = object.value(QLatin1String(key));
    return value.isDouble() && value.toInt() >= 1 && value.toInt() <= 65535;
}

bool boundedFrameTimes(const QJsonObject& payload) {
    const QJsonValue value = payload.value(QStringLiteral("frameTimesMs"));
    if (!value.isArray()) return false;

    const QJsonArray values = value.toArray();
    if (values.isEmpty() || values.size() > 8) return false;
    for (const QJsonValue& sample : values) {
        const double frameMs = sample.toDouble(-1.0);
        if (!sample.isDouble() || !std::isfinite(frameMs) || frameMs <= 0.0 || frameMs > 1000.0) {
            return false;
        }
    }
    return true;
}

bool hasRequiredStrings(const QJsonObject& payload, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        if (!nonEmptyString(payload, key)) {
            return false;
        }
    }
    return true;
}

bool hasOpaqueCrypto(const QJsonObject& payload) {
    const QJsonValue crypto = payload.value(QStringLiteral("crypto"));
    return crypto.isObject() && !crypto.toObject().isEmpty();
}

QJsonValue withoutSecrets(const QJsonValue& value) {
    if (value.isArray()) {
        QJsonArray result;
        for (const QJsonValue& item : value.toArray()) {
            result.append(withoutSecrets(item));
        }
        return result;
    }
    if (!value.isObject()) {
        return value;
    }

    static const QSet<QString> secretKeys{
        QStringLiteral("password"), QStringLiteral("privateKey"),
        QStringLiteral("privateKeyFile"), QStringLiteral("keyFile")
    };
    const QJsonObject object = value.toObject();
    QJsonObject result;
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!secretKeys.contains(it.key())) {
            result.insert(it.key(), withoutSecrets(it.value()));
        }
    }
    return result;
}

bool validatePayload(const QString& type, const QJsonObject& payload) {
    if (type == QStringLiteral("session.connectRemote")) {
        const QJsonValue caFile = payload.value(QStringLiteral("caFile"));
        const QJsonValue tlsServerName = payload.value(QStringLiteral("tlsServerName"));
        return hasRequiredStrings(payload, {"serverIp", "username", "userCode"}) &&
               integerValue(payload, "serverPort") &&
               (caFile.isUndefined() || caFile.isString()) &&
               (tlsServerName.isUndefined() ||
                (tlsServerName.isString() && tlsServerName.toString() == QStringLiteral("localhost")));
    }
    if (type == QStringLiteral("session.connectLocalHost")) {
        return hasRequiredStrings(payload, {"serverExe", "certFile", "keyFile", "dbFile", "username", "userCode"});
    }
    if (type == QStringLiteral("session.discoverLanHosts")) {
        return payload.isEmpty();
    }
    if (type == QStringLiteral("session.connectDiscoveredHost")) {
        return hasRequiredStrings(payload, {"hostId", "username", "userCode"});
    }
    if (type == QStringLiteral("session.disconnect") ||
        type == QStringLiteral("directory.refreshUsers") ||
        type == QStringLiteral("directory.refreshRooms")) {
        return payload.isEmpty();
    }
    if (type == QStringLiteral("chat.sendRoom")) {
        return hasRequiredStrings(payload, {"room"}) &&
               (nonEmptyString(payload, "content") || hasOpaqueCrypto(payload));
    }
    if (type == QStringLiteral("chat.sendPrivate")) {
        return hasRequiredStrings(payload, {"targetUserCode"}) &&
               (nonEmptyString(payload, "content") || hasOpaqueCrypto(payload));
    }
    if (type == QStringLiteral("mls.keyPackage.fetch")) {
        return hasRequiredStrings(payload, {"room", "targetUserCode"});
    }
    if (type == QStringLiteral("mls.group.add") || type == QStringLiteral("mls.group.remove")) {
        return hasRequiredStrings(payload, {"room", "groupId", "targetUserCode"});
    }
    if (type == QStringLiteral("history.search")) {
        return payload.value(QStringLiteral("query")).isString();
    }
    if (type == QStringLiteral("conversation.selectRoom")) {
        return hasRequiredStrings(payload, {"room"});
    }
    if (type == QStringLiteral("conversation.selectDirect")) {
        return hasRequiredStrings(payload, {"userCode"});
    }
    if (type == QStringLiteral("conversation.openPrivate")) {
        return hasRequiredStrings(payload, {"displayName", "userCode"});
    }
    if (type == QStringLiteral("room.create")) {
        return hasRequiredStrings(payload, {"room"}) && booleanValue(payload, "isPrivate");
    }
    if (type == QStringLiteral("room.action")) {
        return hasRequiredStrings(payload, {"action", "room"});
    }
    if (type == QStringLiteral("admin.action")) {
        return hasRequiredStrings(payload, {"action"});
    }
    if (type == QStringLiteral("message.copy")) {
        return payload.value(QStringLiteral("text")).isString();
    }
    if (type == QStringLiteral("message.removeLocal") ||
        type == QStringLiteral("message.recall") || type == QStringLiteral("message.retry")) {
        return hasRequiredStrings(payload, {"messageId"});
    }
    if (type == QStringLiteral("settings.setPerformanceMode")) {
        const QString mode = payload.value(QStringLiteral("mode")).toString();
        return mode == QStringLiteral("Automatic") ||
               mode == QStringLiteral("High") ||
               mode == QStringLiteral("Balanced") ||
               mode == QStringLiteral("Power Saving");
    }
    if (type == QStringLiteral("settings.setConnectionLogging")) {
        return booleanValue(payload, "enabled");
    }
    if (type == QStringLiteral("performance.reportFrameTimes")) {
        return boundedFrameTimes(payload);
    }
    return false;
}

}  // namespace

bool validateCommand(const QJsonObject& command, QString* errorCode) {
    if (errorCode) {
        errorCode->clear();
    }
    if (!nonEmptyString(command, "id") || !nonEmptyString(command, "type") ||
        !command.value(QStringLiteral("payload")).isObject()) {
        setInvalid(errorCode);
        return false;
    }

    if (!validatePayload(command.value(QStringLiteral("type")).toString(),
                         command.value(QStringLiteral("payload")).toObject())) {
        setInvalid(errorCode);
        return false;
    }
    return true;
}

QJsonObject makeCommandResult(const QString& commandId, bool ok, const QJsonObject& error) {
    QJsonObject result{{"id", commandId}, {"ok", ok}};
    if (!ok && !error.isEmpty()) {
        result.insert(QStringLiteral("error"), error);
    }
    return result;
}

QJsonObject makeError(const QString& code, const QString& message, bool retryable,
                      const QString& source, const QString& commandId) {
    QJsonObject error{{"code", code},
                      {"message", message},
                      {"retryable", retryable},
                      {"source", source}};
    if (!commandId.isEmpty()) {
        error.insert(QStringLiteral("commandId"), commandId);
    }
    return error;
}

QString serializeState(const QJsonObject& state) {
    QJsonObject safe = withoutSecrets(state).toObject();
    if (!safe.contains(QStringLiteral("schemaVersion"))) {
        safe.insert(QStringLiteral("schemaVersion"), 1);
    }
    return QString::fromUtf8(QJsonDocument(safe).toJson(QJsonDocument::Compact));
}

}  // namespace bridge
