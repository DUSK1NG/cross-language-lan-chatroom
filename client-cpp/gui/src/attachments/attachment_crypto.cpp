#include "attachments/attachment_crypto.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <limits>
#include <string>

namespace attachments {

bool AttachmentCrypto::generate_key(Key& key) {
    return RAND_bytes(key.data(), static_cast<int>(key.size())) == 1;
}

namespace {

std::string aad_for(const ChunkContext& context) {
    return "lan-chat/attachment-aead/v1\nattachment_id=" + context.attachment_id +
           "\nroom=" + context.room + "\ngroup_id=" + context.group_id +
           "\nlogical_size=" + std::to_string(context.logical_size) +
           "\nchunk_size=" + std::to_string(context.chunk_size) +
           "\nchunk_index=" + std::to_string(context.chunk_index) + "\n";
}

bool valid_context(const ChunkContext& context) {
    return !context.attachment_id.empty() && !context.room.empty() &&
           context.logical_size > 0 && context.chunk_size > 0 && context.chunk_index >= 0;
}

}  // namespace

std::array<std::uint8_t, AttachmentCrypto::NonceSize> AttachmentCrypto::derive_nonce(
    const ChunkContext& context) {
    std::array<std::uint8_t, NonceSize> nonce{};
    const std::string input = "lan-chat/attachment-nonce/v1\n" + aad_for(context);
    EVP_MD_CTX* digest = EVP_MD_CTX_new();
    if (digest == nullptr) return nonce;
    unsigned char hash[EVP_MAX_MD_SIZE]{};
    unsigned int hash_size = 0;
    if (EVP_DigestInit_ex(digest, EVP_sha256(), nullptr) == 1 &&
        EVP_DigestUpdate(digest, input.data(), input.size()) == 1 &&
        EVP_DigestFinal_ex(digest, hash, &hash_size) == 1) {
        std::copy_n(hash, nonce.size(), nonce.begin());
    }
    EVP_MD_CTX_free(digest);
    return nonce;
}

bool AttachmentCrypto::encrypt(const std::array<std::uint8_t, KeySize>& key,
                               const ChunkContext& context,
                               const std::vector<std::uint8_t>& plaintext,
                               std::vector<std::uint8_t>& ciphertext) {
    ciphertext.clear();
    if (!valid_context(context) || plaintext.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    EVP_CIPHER_CTX* cipher = EVP_CIPHER_CTX_new();
    if (cipher == nullptr) return false;
    const auto nonce = derive_nonce(context);
    const std::string aad = aad_for(context);
    std::vector<std::uint8_t> output(plaintext.size() + TagSize);
    int written = 0;
    int final_size = 0;
    bool ok = EVP_EncryptInit_ex(cipher, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
              EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_IVLEN, NonceSize, nullptr) == 1 &&
              EVP_EncryptInit_ex(cipher, nullptr, nullptr, key.data(), nonce.data()) == 1 &&
              EVP_EncryptUpdate(cipher, nullptr, &final_size,
                                reinterpret_cast<const unsigned char*>(aad.data()),
                                static_cast<int>(aad.size())) == 1 &&
              EVP_EncryptUpdate(cipher, output.data(), &written, plaintext.data(),
                                static_cast<int>(plaintext.size())) == 1 &&
              EVP_EncryptFinal_ex(cipher, output.data() + written, &final_size) == 1 &&
              EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_GET_TAG, TagSize,
                                  output.data() + plaintext.size()) == 1;
    EVP_CIPHER_CTX_free(cipher);
    if (!ok) return false;
    ciphertext = std::move(output);
    return true;
}

bool AttachmentCrypto::decrypt(const std::array<std::uint8_t, KeySize>& key,
                               const ChunkContext& context,
                               const std::vector<std::uint8_t>& ciphertext,
                               std::vector<std::uint8_t>& plaintext) {
    plaintext.clear();
    if (!valid_context(context) || ciphertext.size() < TagSize ||
        ciphertext.size() - TagSize > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    EVP_CIPHER_CTX* cipher = EVP_CIPHER_CTX_new();
    if (cipher == nullptr) return false;
    const auto nonce = derive_nonce(context);
    const std::string aad = aad_for(context);
    const std::size_t body_size = ciphertext.size() - TagSize;
    std::vector<std::uint8_t> output(body_size);
    int written = 0;
    int final_size = 0;
    bool ok = EVP_DecryptInit_ex(cipher, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
              EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_IVLEN, NonceSize, nullptr) == 1 &&
              EVP_DecryptInit_ex(cipher, nullptr, nullptr, key.data(), nonce.data()) == 1 &&
              EVP_DecryptUpdate(cipher, nullptr, &final_size,
                                reinterpret_cast<const unsigned char*>(aad.data()),
                                static_cast<int>(aad.size())) == 1 &&
              EVP_DecryptUpdate(cipher, output.data(), &written, ciphertext.data(),
                                static_cast<int>(body_size)) == 1 &&
              EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_TAG, TagSize,
                                  const_cast<std::uint8_t*>(ciphertext.data() + body_size)) == 1 &&
              EVP_DecryptFinal_ex(cipher, output.data() + written, &final_size) == 1;
    EVP_CIPHER_CTX_free(cipher);
    if (!ok) return false;
    plaintext = std::move(output);
    return true;
}

}  // namespace attachments
