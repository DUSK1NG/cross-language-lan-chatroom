#include "attachments/attachment_mls_group_id.hpp"

#include <QCryptographicHash>

namespace attachments {

QString attachment_mls_group_id(const QString& room, const QString& commandId) {
    const QByteArray material = room.trimmed().toUtf8() + '\0' + commandId.trimmed().toUtf8();
    const QByteArray digest = QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex();
    return QStringLiteral("lan-chat-attachment/") + QString::fromLatin1(digest);
}

}  // namespace attachments
