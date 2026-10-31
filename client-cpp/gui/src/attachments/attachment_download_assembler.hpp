#pragma once

#include "attachments/attachment_crypto.hpp"

#include <QByteArray>
#include <QString>

namespace attachments {

// Writes decrypted chunks to "<outputPath>.lanchat-part" and renames it to
// outputPath only after the last chunk verifies. Any error, cancel() or
// destruction before completion deletes the partial file; an existing file at
// outputPath is left untouched unless the download completes.
class AttachmentDownloadAssembler final {
public:
    AttachmentDownloadAssembler() = default;
    ~AttachmentDownloadAssembler();
    AttachmentDownloadAssembler(const AttachmentDownloadAssembler&) = delete;
    AttachmentDownloadAssembler& operator=(const AttachmentDownloadAssembler&) = delete;
    AttachmentDownloadAssembler(AttachmentDownloadAssembler&& other) noexcept;
    AttachmentDownloadAssembler& operator=(AttachmentDownloadAssembler&& other) noexcept;

    bool begin(const QString& outputPath, const AttachmentCrypto::Key& key,
               ChunkContext context, QString* error = nullptr);
    bool acceptChunk(std::int64_t index, const QByteArray& ciphertext,
                     const QByteArray& cipherSha256, bool last, QString* error = nullptr);
    void cancel();
    bool complete() const { return complete_; }

private:
    bool fail(const QString& reason, QString* error);
    bool finalize(QString* error);
    void discardPart();

    QString outputPath_;
    QString partPath_;
    AttachmentCrypto::Key key_{};
    ChunkContext context_;
    std::int64_t nextIndex_ = 0;
    bool complete_ = false;
};

}  // namespace attachments
