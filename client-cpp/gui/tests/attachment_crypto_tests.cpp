#include "attachments/attachment_crypto.hpp"

#include <QtTest/QtTest>

#include <array>

class AttachmentCryptoTests : public QObject {
    Q_OBJECT

private slots:
    void generatesIndependentKeys();
    void roundTrip();
    void nonceIsDeterministicAndChunkBound();
    void tamperingIsRejected();
};

void AttachmentCryptoTests::generatesIndependentKeys() {
    attachments::AttachmentCrypto::Key first{};
    attachments::AttachmentCrypto::Key second{};
    QVERIFY(attachments::AttachmentCrypto::generate_key(first));
    QVERIFY(attachments::AttachmentCrypto::generate_key(second));
    QVERIFY(first != attachments::AttachmentCrypto::Key{});
    QVERIFY(second != attachments::AttachmentCrypto::Key{});
    QVERIFY(first != second);
}

namespace {

attachments::ChunkContext context() {
    return {"attachment-1", "lobby", "group-1", 123456, 48128, 3};
}

std::array<std::uint8_t, attachments::AttachmentCrypto::KeySize> key() {
    std::array<std::uint8_t, attachments::AttachmentCrypto::KeySize> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::uint8_t>(index + 1);
    }
    return result;
}

}  // namespace

void AttachmentCryptoTests::roundTrip() {
    const std::vector<std::uint8_t> plaintext{'s', 'e', 'c', 'r', 'e', 't'};
    std::vector<std::uint8_t> ciphertext;
    QVERIFY(attachments::AttachmentCrypto::encrypt(key(), context(), plaintext, ciphertext));
    QCOMPARE(ciphertext.size(), plaintext.size() + attachments::AttachmentCrypto::TagSize);
    QVERIFY(ciphertext != plaintext);

    std::vector<std::uint8_t> recovered;
    QVERIFY(attachments::AttachmentCrypto::decrypt(key(), context(), ciphertext, recovered));
    QCOMPARE(recovered, plaintext);
}

void AttachmentCryptoTests::nonceIsDeterministicAndChunkBound() {
    const auto first = attachments::AttachmentCrypto::derive_nonce(context());
    const auto second = attachments::AttachmentCrypto::derive_nonce(context());
    QCOMPARE(first, second);
    auto changed = context();
    changed.chunk_index++;
    QVERIFY(first != attachments::AttachmentCrypto::derive_nonce(changed));
}

void AttachmentCryptoTests::tamperingIsRejected() {
    std::vector<std::uint8_t> ciphertext;
    QVERIFY(attachments::AttachmentCrypto::encrypt(key(), context(), {'a', 'b', 'c'}, ciphertext));
    ciphertext[0] ^= 0x01;
    std::vector<std::uint8_t> plaintext;
    QVERIFY(!attachments::AttachmentCrypto::decrypt(key(), context(), ciphertext, plaintext));
}

QTEST_APPLESS_MAIN(AttachmentCryptoTests)

#include "attachment_crypto_tests.moc"
