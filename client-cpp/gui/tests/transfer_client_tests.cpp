#include "attachments/transfer_client.hpp"

#include <QBuffer>
#include <QtTest/QtTest>

#include <array>

class TransferClientTests : public QObject {
    Q_OBJECT

private slots:
    void streamsAndEncryptsWithoutLoadingWholeInput();
    void rejectsShortInput();
    void rejectsMissingFile();
    void encryptsOneChunkAtATime();
};

void TransferClientTests::streamsAndEncryptsWithoutLoadingWholeInput() {
    QByteArray input(attachments::TransferClient::ChunkSize + 1, 'x');
    QBuffer buffer(&input);
    QVERIFY(buffer.open(QIODevice::ReadOnly));
    std::array<std::uint8_t, attachments::AttachmentCrypto::KeySize> key{};
    attachments::ChunkContext context{"attachment-1", "lobby", "group-1", input.size(), attachments::TransferClient::ChunkSize, 0};
    QList<attachments::TransferChunk> chunks;
    QVERIFY(attachments::TransferClient::stream_encrypt(buffer, key, context,
        [&](const attachments::TransferChunk& chunk) { chunks.append(chunk); return true; }));
    QCOMPARE(chunks.size(), 2);
    QCOMPARE(chunks[0].index, std::int64_t(0));
    QCOMPARE(chunks[1].index, std::int64_t(1));
    QCOMPARE(chunks[0].ciphertext.size(), attachments::TransferClient::ChunkSize + attachments::AttachmentCrypto::TagSize);
    QCOMPARE(chunks[1].ciphertext.size(), 1 + attachments::AttachmentCrypto::TagSize);
}

void TransferClientTests::rejectsShortInput() {
    QByteArray input("short");
    QBuffer buffer(&input);
    QVERIFY(buffer.open(QIODevice::ReadOnly));
    std::array<std::uint8_t, attachments::AttachmentCrypto::KeySize> key{};
    attachments::ChunkContext context{"attachment-1", "lobby", "group-1", 10, attachments::TransferClient::ChunkSize, 0};
    QString error;
    QVERIFY(!attachments::TransferClient::stream_encrypt(buffer, key, context, [](const auto&) { return true; }, &error));
    QVERIFY(!error.isEmpty());
}

void TransferClientTests::rejectsMissingFile() {
    std::array<std::uint8_t, attachments::AttachmentCrypto::KeySize> key{};
    attachments::ChunkContext context{"attachment-1", "lobby", "group-1", 1,
                                      attachments::TransferClient::ChunkSize, 0};
    QString error;
    QVERIFY(!attachments::TransferClient::stream_encrypt_file(
        QStringLiteral("C:/lan-chat-file-that-does-not-exist"), key, context,
        [](const attachments::TransferChunk&) { return true; }, &error));
    QCOMPARE(error, QStringLiteral("unable to open attachment file"));
}

void TransferClientTests::encryptsOneChunkAtATime() {
    QByteArray input(attachments::TransferClient::ChunkSize + 3, 'z');
    QBuffer buffer(&input);
    QVERIFY(buffer.open(QIODevice::ReadOnly));
    std::array<std::uint8_t, attachments::AttachmentCrypto::KeySize> key{};
    attachments::ChunkContext context{"attachment-1", "lobby", "group-1", input.size(),
                                      attachments::TransferClient::ChunkSize, 0};
    attachments::TransferChunk first;
    bool eof = false;
    QVERIFY(attachments::TransferClient::encrypt_next_chunk(buffer, key, context, first, eof));
    QVERIFY(!eof);
    QCOMPARE(first.index, std::int64_t(0));
    context.chunk_index = 1;
    attachments::TransferChunk second;
    QVERIFY(attachments::TransferClient::encrypt_next_chunk(buffer, key, context, second, eof));
    QVERIFY(eof);
    QCOMPARE(second.ciphertext.size(), 3 + attachments::AttachmentCrypto::TagSize);
}

QTEST_APPLESS_MAIN(TransferClientTests)

#include "transfer_client_tests.moc"
