#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace attachments {

struct ChunkContext {
    std::string attachment_id;
    std::string room;
    std::string group_id;
    std::int64_t logical_size = 0;
    std::int64_t chunk_size = 0;
    std::int64_t chunk_index = 0;
};

class AttachmentCrypto {
public:
    static constexpr std::size_t KeySize = 32;
    static constexpr std::size_t NonceSize = 12;
    static constexpr std::size_t TagSize = 16;
    using Key = std::array<std::uint8_t, KeySize>;

    static bool generate_key(Key& key);

    static std::array<std::uint8_t, NonceSize> derive_nonce(const ChunkContext& context);
    static bool encrypt(const Key& key,
                        const ChunkContext& context,
                        const std::vector<std::uint8_t>& plaintext,
                        std::vector<std::uint8_t>& ciphertext);
    static bool decrypt(const Key& key,
                        const ChunkContext& context,
                        const std::vector<std::uint8_t>& ciphertext,
                        std::vector<std::uint8_t>& plaintext);
};

}  // namespace attachments
