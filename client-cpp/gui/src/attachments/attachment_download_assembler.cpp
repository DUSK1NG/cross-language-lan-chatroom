#include "attachments/attachment_download_assembler.hpp"

#include <QCryptographicHash>
#include <QFile>

#include <algorithm>
#include <utility>
#include <vector>

namespace attachments {

bool AttachmentDownloadAssembler::begin(const QString& outputPath, const AttachmentCrypto::Key& key,
                                        ChunkContext context, QString* error) {
    if (outputPath.isEmpty() || context.logical_size <= 0 || context.chunk_size <= 0 ||
        context.chunk_index != 0 || context.attachment_id.empty() || context.room.empty()) {
        if (error) *error = QStringLiteral("invalid attachment download context");
        return false;
    }
    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        !output.resize(context.logical_size)) {
        if (error) *error = QStringLiteral("unable to create attachment output file");
        return false;
    }
    outputPath_ = outputPath;
    key_ = key;
    context_ = std::move(context);
    nextIndex_ = 0;
    complete_ = false;
    return true;
}

bool AttachmentDownloadAssembler::acceptChunk(std::int64_t index, const QByteArray& ciphertext,
                                               const QByteArray& cipherSha256, bool last,
                                               QString* error) {
    if (complete_ || index != nextIndex_ || ciphertext.isEmpty() || cipherSha256.size() != 64) {
        if (error) *error = QStringLiteral("unexpected attachment download chunk");
        return false;
    }
    if (QCryptographicHash::hash(ciphertext, QCryptographicHash::Sha256).toHex() != cipherSha256.toLower()) {
        if (error) *error = QStringLiteral("attachment ciphertext hash mismatch");
        return false;
    }
    const auto chunkCount = (context_.logical_size + context_.chunk_size - 1) / context_.chunk_size;
    const auto remaining = context_.logical_size - index * context_.chunk_size;
    const auto expectedPlaintext = std::min(context_.chunk_size, remaining);
    if (index < 0 || index >= chunkCount || expectedPlaintext <= 0 ||
        ciphertext.size() != expectedPlaintext + static_cast<std::int64_t>(AttachmentCrypto::TagSize)) {
        if (error) *error = QStringLiteral("invalid attachment download chunk size");
        return false;
    }
    ChunkContext chunkContext = context_;
    chunkContext.chunk_index = index;
    std::vector<std::uint8_t> encrypted(ciphertext.begin(), ciphertext.end());
    std::vector<std::uint8_t> plaintext;
    if (!AttachmentCrypto::decrypt(key_, chunkContext, encrypted, plaintext) ||
        plaintext.size() != static_cast<std::size_t>(expectedPlaintext)) {
        if (error) *error = QStringLiteral("attachment decryption failed");
        return false;
    }
    QFile output(outputPath_);
    if (!output.open(QIODevice::ReadWrite) || !output.seek(index * context_.chunk_size) ||
        output.write(reinterpret_cast<const char*>(plaintext.data()), static_cast<qint64>(plaintext.size())) !=
            static_cast<qint64>(plaintext.size())) {
        if (error) *error = QStringLiteral("unable to write attachment output file");
        return false;
    }
    const bool expectedLast = index + 1 == chunkCount;
    if (last != expectedLast) {
        if (error) *error = QStringLiteral("attachment last-chunk marker mismatch");
        return false;
    }
    ++nextIndex_;
    complete_ = expectedLast;
    return true;
}

}  // namespace attachments
