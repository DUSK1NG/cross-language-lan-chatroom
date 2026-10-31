#include "attachments/attachment_download_assembler.hpp"

#include <QCryptographicHash>
#include <QFile>

#include <algorithm>
#include <utility>
#include <vector>

namespace attachments {

namespace {

QString partPathFor(const QString& outputPath) { return outputPath + QStringLiteral(".lanchat-part"); }

}  // namespace

AttachmentDownloadAssembler::~AttachmentDownloadAssembler() { discardPart(); }

AttachmentDownloadAssembler::AttachmentDownloadAssembler(AttachmentDownloadAssembler&& other) noexcept
    : outputPath_(std::move(other.outputPath_)),
      partPath_(std::exchange(other.partPath_, QString())),
      key_(other.key_),
      context_(std::move(other.context_)),
      nextIndex_(other.nextIndex_),
      complete_(other.complete_) {}

AttachmentDownloadAssembler& AttachmentDownloadAssembler::operator=(AttachmentDownloadAssembler&& other) noexcept {
    if (this == &other) return *this;
    discardPart();
    outputPath_ = std::move(other.outputPath_);
    partPath_ = std::exchange(other.partPath_, QString());
    key_ = other.key_;
    context_ = std::move(other.context_);
    nextIndex_ = other.nextIndex_;
    complete_ = other.complete_;
    return *this;
}

bool AttachmentDownloadAssembler::begin(const QString& outputPath, const AttachmentCrypto::Key& key,
                                        ChunkContext context, QString* error) {
    discardPart();
    complete_ = false;
    if (outputPath.isEmpty() || context.logical_size <= 0 || context.chunk_size <= 0 ||
        context.chunk_index != 0 || context.attachment_id.empty() || context.room.empty()) {
        if (error) *error = QStringLiteral("invalid attachment download context");
        return false;
    }
    const QString partPath = partPathFor(outputPath);
    QFile output(partPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        !output.resize(context.logical_size)) {
        output.close();
        QFile::remove(partPath);
        if (error) *error = QStringLiteral("unable to create attachment output file");
        return false;
    }
    outputPath_ = outputPath;
    partPath_ = partPath;
    key_ = key;
    context_ = std::move(context);
    nextIndex_ = 0;
    return true;
}

bool AttachmentDownloadAssembler::acceptChunk(std::int64_t index, const QByteArray& ciphertext,
                                               const QByteArray& cipherSha256, bool last,
                                               QString* error) {
    if (partPath_.isEmpty()) {
        if (error) *error = QStringLiteral("unexpected attachment download chunk");
        return false;
    }
    if (index != nextIndex_ || ciphertext.isEmpty() || cipherSha256.size() != 64) {
        return fail(QStringLiteral("unexpected attachment download chunk"), error);
    }
    if (QCryptographicHash::hash(ciphertext, QCryptographicHash::Sha256).toHex() != cipherSha256.toLower()) {
        return fail(QStringLiteral("attachment ciphertext hash mismatch"), error);
    }
    const auto chunkCount = (context_.logical_size + context_.chunk_size - 1) / context_.chunk_size;
    const auto remaining = context_.logical_size - index * context_.chunk_size;
    const auto expectedPlaintext = std::min(context_.chunk_size, remaining);
    if (index < 0 || index >= chunkCount || expectedPlaintext <= 0 ||
        ciphertext.size() != expectedPlaintext + static_cast<std::int64_t>(AttachmentCrypto::TagSize)) {
        return fail(QStringLiteral("invalid attachment download chunk size"), error);
    }
    ChunkContext chunkContext = context_;
    chunkContext.chunk_index = index;
    std::vector<std::uint8_t> encrypted(ciphertext.begin(), ciphertext.end());
    std::vector<std::uint8_t> plaintext;
    if (!AttachmentCrypto::decrypt(key_, chunkContext, encrypted, plaintext) ||
        plaintext.size() != static_cast<std::size_t>(expectedPlaintext)) {
        return fail(QStringLiteral("attachment decryption failed"), error);
    }
    {
        QFile output(partPath_);
        if (!output.open(QIODevice::ReadWrite) || !output.seek(index * context_.chunk_size) ||
            output.write(reinterpret_cast<const char*>(plaintext.data()), static_cast<qint64>(plaintext.size())) !=
                static_cast<qint64>(plaintext.size())) {
            output.close();
            return fail(QStringLiteral("unable to write attachment output file"), error);
        }
    }
    const bool expectedLast = index + 1 == chunkCount;
    if (last != expectedLast) {
        return fail(QStringLiteral("attachment last-chunk marker mismatch"), error);
    }
    ++nextIndex_;
    if (expectedLast) return finalize(error);
    return true;
}

void AttachmentDownloadAssembler::cancel() { discardPart(); }

bool AttachmentDownloadAssembler::fail(const QString& reason, QString* error) {
    discardPart();
    if (error) *error = reason;
    return false;
}

bool AttachmentDownloadAssembler::finalize(QString* error) {
    // QFile::rename does not overwrite on Windows; the existing target is only
    // removed once the whole download has verified.
    if (QFile::exists(outputPath_) && !QFile::remove(outputPath_)) {
        return fail(QStringLiteral("unable to replace existing attachment output file"), error);
    }
    if (!QFile::rename(partPath_, outputPath_)) {
        return fail(QStringLiteral("unable to move attachment into place"), error);
    }
    partPath_.clear();
    complete_ = true;
    return true;
}

void AttachmentDownloadAssembler::discardPart() {
    if (partPath_.isEmpty()) return;
    QFile::remove(partPath_);
    partPath_.clear();
}

}  // namespace attachments
