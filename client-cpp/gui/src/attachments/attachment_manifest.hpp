#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace attachments {

// This structure is serialized before MLS protection. Never put it in a clear-text
// attachment command or expose key to the bridge/UI layer.
struct AttachmentManifest {
    QString attachmentId;
    QString fileName;
    QString room;
    QString groupId;
    qint64 logicalSize = 0;
    qint64 chunkSize = 0;
    quint64 epoch = 0;
    QByteArray key;

    QByteArray encode() const;
    static bool decode(const QByteArray& encoded, AttachmentManifest& manifest);
};

// A manifest is retried as the same logical chat message. Keep its wire
// message ID derived from the server-issued attachment ID rather than from a
// transient upload or connection state.
QString manifestMessageId(const QString& attachmentId);

}  // namespace attachments
