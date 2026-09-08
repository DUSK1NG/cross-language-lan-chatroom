#pragma once

#include "attachments/attachment_crypto.hpp"
#include "../../../include/attachment_limits.hpp"

#include <QByteArray>
#include <QIODevice>
#include <QString>

#include <array>
#include <cstdint>
#include <functional>

namespace attachments {

struct TransferChunk {
    std::int64_t index = 0;
    QByteArray ciphertext;
    QByteArray cipher_sha256;
};

class TransferClient {
public:
    using ChunkSink = std::function<bool(const TransferChunk&)>;

    static constexpr std::int64_t ChunkSize = PlaintextChunkBytes;

    static bool stream_encrypt(QIODevice& input,
                               const std::array<std::uint8_t, AttachmentCrypto::KeySize>& key,
                               ChunkContext context,
                               const ChunkSink& sink,
                               QString* error = nullptr);
    static bool stream_encrypt_file(const QString& filePath,
                                    const std::array<std::uint8_t, AttachmentCrypto::KeySize>& key,
                                    ChunkContext context,
                                    const ChunkSink& sink,
                                    QString* error = nullptr);
    static bool encrypt_next_chunk(QIODevice& input,
                                   const std::array<std::uint8_t, AttachmentCrypto::KeySize>& key,
                                   ChunkContext context,
                                   TransferChunk& chunk,
                                   bool& endOfFile,
                                   QString* error = nullptr);
};

}  // namespace attachments
