#pragma once

#include "attachments/attachment_crypto.hpp"

#include <QByteArray>
#include <QString>

namespace attachments {

class AttachmentDownloadAssembler final {
public:
    bool begin(const QString& outputPath, const AttachmentCrypto::Key& key,
               ChunkContext context, QString* error = nullptr);
    bool acceptChunk(std::int64_t index, const QByteArray& ciphertext,
                     const QByteArray& cipherSha256, bool last, QString* error = nullptr);
    bool complete() const { return complete_; }

private:
    QString outputPath_;
    AttachmentCrypto::Key key_{};
    ChunkContext context_;
    std::int64_t nextIndex_ = 0;
    bool complete_ = false;
};

}  // namespace attachments
