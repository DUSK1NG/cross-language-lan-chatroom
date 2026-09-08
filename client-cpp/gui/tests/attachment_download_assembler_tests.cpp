#include "attachments/attachment_download_assembler.hpp"

#include <QFile>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <vector>

class AttachmentDownloadAssemblerTests final : public QObject {
    Q_OBJECT

private slots:
    void verifiesAndAssemblesEncryptedChunks();
    void rejectsTamperedChunk();
};

void AttachmentDownloadAssemblerTests::verifiesAndAssemblesEncryptedChunks() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("received.bin"));
    attachments::AttachmentCrypto::Key key{};
    const QByteArray plaintext("hello encrypted world");
    const auto chunkSize = std::int64_t(12);
    attachments::ChunkContext context{"attachment-1", "lobby", "group-1", plaintext.size(), chunkSize, 0};
    std::vector<QByteArray> encrypted;
    for (std::int64_t index = 0; index < 2; ++index) {
        context.chunk_index = index;
        const QByteArray part = plaintext.mid(static_cast<int>(index * chunkSize), static_cast<int>(chunkSize));
        std::vector<std::uint8_t> input(part.begin(), part.end());
        std::vector<std::uint8_t> output;
        QVERIFY(attachments::AttachmentCrypto::encrypt(key, context, input, output));
        encrypted.emplace_back(reinterpret_cast<const char*>(output.data()), static_cast<int>(output.size()));
    }
    attachments::AttachmentDownloadAssembler assembler;
    context.chunk_index = 0;
    QVERIFY(assembler.begin(path, key, context));
    QVERIFY(assembler.acceptChunk(0, encrypted[0], QCryptographicHash::hash(encrypted[0], QCryptographicHash::Sha256).toHex(), false));
    QVERIFY(assembler.acceptChunk(1, encrypted[1], QCryptographicHash::hash(encrypted[1], QCryptographicHash::Sha256).toHex(), true));
    QVERIFY(assembler.complete());
    QFile output(path);
    QVERIFY(output.open(QIODevice::ReadOnly));
    QCOMPARE(output.readAll(), plaintext);
}

void AttachmentDownloadAssemblerTests::rejectsTamperedChunk() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    attachments::AttachmentCrypto::Key key{};
    attachments::ChunkContext context{"attachment-1", "lobby", "group-1", 3, 12, 0};
    std::vector<std::uint8_t> input{'a', 'b', 'c'};
    std::vector<std::uint8_t> output;
    QVERIFY(attachments::AttachmentCrypto::encrypt(key, context, input, output));
    QByteArray ciphertext(reinterpret_cast<const char*>(output.data()), static_cast<int>(output.size()));
    ciphertext[0] = static_cast<char>(ciphertext[0] ^ 1);
    attachments::AttachmentDownloadAssembler assembler;
    QVERIFY(assembler.begin(directory.filePath(QStringLiteral("received.bin")), key, context));
    QVERIFY(!assembler.acceptChunk(0, ciphertext, QCryptographicHash::hash(ciphertext, QCryptographicHash::Sha256).toHex(), true));
    QVERIFY(!assembler.complete());
}

QTEST_APPLESS_MAIN(AttachmentDownloadAssemblerTests)

#include "attachment_download_assembler_tests.moc"
