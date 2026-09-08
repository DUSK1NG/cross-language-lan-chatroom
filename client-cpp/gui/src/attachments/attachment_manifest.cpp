#include "attachments/attachment_manifest.hpp"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace attachments {
namespace {

bool valid(const AttachmentManifest& manifest) {
    return !manifest.attachmentId.trimmed().isEmpty() &&
           !manifest.fileName.trimmed().isEmpty() &&
           !manifest.room.trimmed().isEmpty() &&
           !manifest.groupId.trimmed().isEmpty() &&
           manifest.logicalSize > 0 && manifest.chunkSize > 0 && manifest.epoch > 0 &&
           manifest.key.size() == 32;
}

}  // namespace

QByteArray AttachmentManifest::encode() const {
    if (!valid(*this)) return {};
    const QJsonObject object{
        {QStringLiteral("v"), 1},
        {QStringLiteral("attachment_id"), attachmentId},
        {QStringLiteral("file_name"), fileName},
        {QStringLiteral("room"), room},
        {QStringLiteral("group_id"), groupId},
        {QStringLiteral("logical_size"), logicalSize},
        {QStringLiteral("chunk_size"), chunkSize},
        {QStringLiteral("epoch"), static_cast<qint64>(epoch)},
        {QStringLiteral("key"), QString::fromLatin1(key.toBase64())},
    };
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool AttachmentManifest::decode(const QByteArray& encoded, AttachmentManifest& manifest) {
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(encoded, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("v")).toInt(-1) != 1 ||
        !object.value(QStringLiteral("attachment_id")).isString() ||
        !object.value(QStringLiteral("file_name")).isString() ||
        !object.value(QStringLiteral("room")).isString() ||
        !object.value(QStringLiteral("group_id")).isString() ||
        !object.value(QStringLiteral("logical_size")).isDouble() ||
        !object.value(QStringLiteral("chunk_size")).isDouble() ||
        !object.value(QStringLiteral("epoch")).isDouble() ||
        !object.value(QStringLiteral("key")).isString()) {
        return false;
    }
    AttachmentManifest candidate;
    candidate.attachmentId = object.value(QStringLiteral("attachment_id")).toString();
    candidate.fileName = object.value(QStringLiteral("file_name")).toString();
    candidate.room = object.value(QStringLiteral("room")).toString();
    candidate.groupId = object.value(QStringLiteral("group_id")).toString();
    candidate.logicalSize = object.value(QStringLiteral("logical_size")).toVariant().toLongLong();
    candidate.chunkSize = object.value(QStringLiteral("chunk_size")).toVariant().toLongLong();
    candidate.epoch = object.value(QStringLiteral("epoch")).toVariant().toULongLong();
    candidate.key = QByteArray::fromBase64(object.value(QStringLiteral("key")).toString().toLatin1());
    if (!valid(candidate)) return false;
    manifest = std::move(candidate);
    return true;
}

QString manifestMessageId(const QString& attachmentId) {
    const QByteArray normalized = attachmentId.trimmed().toUtf8();
    if (normalized.isEmpty()) return {};
    const QByteArray digest = QCryptographicHash::hash(normalized, QCryptographicHash::Sha256).toHex();
    // The Go transport accepts at most 64 bytes for command IDs. Keep this
    // message ID below that same bound so it is safe if a future command
    // references it directly.
    return QStringLiteral("am-") + QString::fromLatin1(digest.left(56));
}

}  // namespace attachments
