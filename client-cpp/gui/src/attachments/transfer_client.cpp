#include "attachments/transfer_client.hpp"

#include <QCryptographicHash>
#include <QFile>

#include <algorithm>
#include <vector>

namespace attachments {

bool TransferClient::encrypt_next_chunk(
    QIODevice& input,
    const std::array<std::uint8_t, AttachmentCrypto::KeySize>& key,
    ChunkContext context,
    TransferChunk& chunk,
    bool& endOfFile,
    QString* error) {
    chunk = {};
    endOfFile = false;
    if (!input.isOpen() || !input.isReadable() || context.logical_size <= 0 ||
        context.chunk_size <= 0 || context.chunk_index < 0) {
        if (error) *error = QStringLiteral("invalid attachment transfer context");
        return false;
    }
    const auto remaining = context.logical_size - context.chunk_index * context.chunk_size;
    if (remaining <= 0) {
        endOfFile = true;
        return true;
    }
    const auto readSize = static_cast<int>(std::min<std::int64_t>(context.chunk_size, remaining));
    const QByteArray bytes = input.read(readSize);
    if (bytes.size() != readSize) {
        if (error) *error = QStringLiteral("attachment file ended before logical size");
        return false;
    }
    std::vector<std::uint8_t> plaintext(bytes.begin(), bytes.end());
    std::vector<std::uint8_t> ciphertext;
    if (!AttachmentCrypto::encrypt(key, context, plaintext, ciphertext)) {
        if (error) *error = QStringLiteral("attachment encryption failed");
        return false;
    }
    chunk.index = context.chunk_index;
    chunk.ciphertext = QByteArray(reinterpret_cast<const char*>(ciphertext.data()),
                                  static_cast<int>(ciphertext.size()));
    chunk.cipher_sha256 = QCryptographicHash::hash(chunk.ciphertext, QCryptographicHash::Sha256).toHex();
    endOfFile = context.chunk_index + 1 >=
                (context.logical_size + context.chunk_size - 1) / context.chunk_size;
    return true;
}

bool TransferClient::stream_encrypt_file(
    const QString& filePath,
    const std::array<std::uint8_t, AttachmentCrypto::KeySize>& key,
    const ChunkContext context,
    const ChunkSink& sink,
    QString* error) {
    QFile input(filePath);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("unable to open attachment file");
        return false;
    }
    return stream_encrypt(input, key, context, sink, error);
}

bool TransferClient::stream_encrypt(
    QIODevice& input,
    const std::array<std::uint8_t, AttachmentCrypto::KeySize>& key,
    ChunkContext context,
    const ChunkSink& sink,
    QString* error) {
    if (!input.isOpen() || !input.isReadable() || !sink || context.logical_size < 0) {
        if (error) *error = QStringLiteral("invalid transfer input");
        return false;
    }
    if (context.chunk_size == 0) context.chunk_size = ChunkSize;
    if (context.chunk_size != ChunkSize) {
        if (error) *error = QStringLiteral("unsupported chunk size");
        return false;
    }
    std::int64_t index = 0;
    std::int64_t remaining = context.logical_size;
    while (remaining > 0) {
        const qint64 requested = static_cast<qint64>(std::min<std::int64_t>(remaining, ChunkSize));
        const QByteArray bytes = input.read(requested);
        if (bytes.size() != requested) {
            if (error) *error = QStringLiteral("input ended before logical size");
            return false;
        }
        context.chunk_index = index++;
        std::vector<std::uint8_t> plaintext(bytes.begin(), bytes.end());
        std::vector<std::uint8_t> encrypted;
        if (!AttachmentCrypto::encrypt(key, context, plaintext, encrypted)) {
            if (error) *error = QStringLiteral("attachment encryption failed");
            return false;
        }
        const QByteArray ciphertext(reinterpret_cast<const char*>(encrypted.data()),
                                    static_cast<int>(encrypted.size()));
        const QByteArray hash = QCryptographicHash::hash(ciphertext, QCryptographicHash::Sha256).toHex();
        if (!sink({context.chunk_index, ciphertext, hash})) {
            if (error) *error = QStringLiteral("chunk sink rejected transfer");
            return false;
        }
        remaining -= requested;
    }
    return true;
}

}  // namespace attachments
