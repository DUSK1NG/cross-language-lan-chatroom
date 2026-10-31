#include "attachments/attachment_download_assembler.hpp"

#include <QFile>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <utility>
#include <vector>

namespace {

const QByteArray kPlaintext("hello encrypted world");
constexpr std::int64_t kChunkSize = 12;

attachments::ChunkContext twoChunkContext() {
    return {"attachment-1", "lobby", "group-1", kPlaintext.size(), kChunkSize, 0};
}

std::vector<QByteArray> encryptTwoChunks(const attachments::AttachmentCrypto::Key& key) {
    auto context = twoChunkContext();
    std::vector<QByteArray> encrypted;
    for (std::int64_t index = 0; index < 2; ++index) {
        context.chunk_index = index;
        const QByteArray part = kPlaintext.mid(static_cast<int>(index * kChunkSize), static_cast<int>(kChunkSize));
        std::vector<std::uint8_t> input(part.begin(), part.end());
        std::vector<std::uint8_t> output;
        if (!attachments::AttachmentCrypto::encrypt(key, context, input, output)) return {};
        encrypted.emplace_back(reinterpret_cast<const char*>(output.data()), static_cast<int>(output.size()));
    }
    return encrypted;
}

QByteArray sha256Hex(const QByteArray& data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

QString partPath(const QString& path) { return path + QStringLiteral(".lanchat-part"); }

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(content), static_cast<qint64>(content.size()));
}

}  // namespace

class AttachmentDownloadAssemblerTests final : public QObject {
    Q_OBJECT

private slots:
    void verifiesAndAssemblesEncryptedChunks();
    void rejectsTamperedChunk();
    void failurePreservesExistingTarget();
    void cancelRemovesPartialFile();
    void destructionRemovesPartialFileButMoveDoesNot();
};

void AttachmentDownloadAssemblerTests::verifiesAndAssemblesEncryptedChunks() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("received.bin"));
    writeFile(path, QByteArrayLiteral("previous content"));
    attachments::AttachmentCrypto::Key key{};
    const auto encrypted = encryptTwoChunks(key);
    QCOMPARE(encrypted.size(), std::size_t(2));
    attachments::AttachmentDownloadAssembler assembler;
    QVERIFY(assembler.begin(path, key, twoChunkContext()));
    QVERIFY(assembler.acceptChunk(0, encrypted[0], sha256Hex(encrypted[0]), false));
    QCOMPARE(readFile(path), QByteArrayLiteral("previous content"));
    QVERIFY(QFile::exists(partPath(path)));
    QVERIFY(assembler.acceptChunk(1, encrypted[1], sha256Hex(encrypted[1]), true));
    QVERIFY(assembler.complete());
    QCOMPARE(readFile(path), kPlaintext);
    QVERIFY(!QFile::exists(partPath(path)));
}

void AttachmentDownloadAssemblerTests::rejectsTamperedChunk() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("received.bin"));
    attachments::AttachmentCrypto::Key key{};
    attachments::ChunkContext context{"attachment-1", "lobby", "group-1", 3, 12, 0};
    std::vector<std::uint8_t> input{'a', 'b', 'c'};
    std::vector<std::uint8_t> output;
    QVERIFY(attachments::AttachmentCrypto::encrypt(key, context, input, output));
    QByteArray ciphertext(reinterpret_cast<const char*>(output.data()), static_cast<int>(output.size()));
    ciphertext[0] = static_cast<char>(ciphertext[0] ^ 1);
    attachments::AttachmentDownloadAssembler assembler;
    QVERIFY(assembler.begin(path, key, context));
    QVERIFY(!assembler.acceptChunk(0, ciphertext, sha256Hex(ciphertext), true));
    QVERIFY(!assembler.complete());
    QVERIFY(!QFile::exists(path));
    QVERIFY(!QFile::exists(partPath(path)));
}

void AttachmentDownloadAssemblerTests::failurePreservesExistingTarget() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("received.bin"));
    writeFile(path, QByteArrayLiteral("keep me"));
    attachments::AttachmentCrypto::Key key{};
    auto encrypted = encryptTwoChunks(key);
    QCOMPARE(encrypted.size(), std::size_t(2));
    attachments::AttachmentDownloadAssembler assembler;
    QVERIFY(assembler.begin(path, key, twoChunkContext()));
    QVERIFY(assembler.acceptChunk(0, encrypted[0], sha256Hex(encrypted[0]), false));
    encrypted[1][0] = static_cast<char>(encrypted[1][0] ^ 1);
    QVERIFY(!assembler.acceptChunk(1, encrypted[1], sha256Hex(encrypted[1]), true));
    QCOMPARE(readFile(path), QByteArrayLiteral("keep me"));
    QVERIFY(!QFile::exists(partPath(path)));
}

void AttachmentDownloadAssemblerTests::cancelRemovesPartialFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("received.bin"));
    attachments::AttachmentCrypto::Key key{};
    const auto encrypted = encryptTwoChunks(key);
    QCOMPARE(encrypted.size(), std::size_t(2));
    attachments::AttachmentDownloadAssembler assembler;
    QVERIFY(assembler.begin(path, key, twoChunkContext()));
    QVERIFY(assembler.acceptChunk(0, encrypted[0], sha256Hex(encrypted[0]), false));
    assembler.cancel();
    QVERIFY(!QFile::exists(path));
    QVERIFY(!QFile::exists(partPath(path)));
    QVERIFY(!assembler.acceptChunk(1, encrypted[1], sha256Hex(encrypted[1]), true));
    QVERIFY(!QFile::exists(path));
}

void AttachmentDownloadAssemblerTests::destructionRemovesPartialFileButMoveDoesNot() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("received.bin"));
    attachments::AttachmentCrypto::Key key{};
    const auto encrypted = encryptTwoChunks(key);
    QCOMPARE(encrypted.size(), std::size_t(2));
    {
        attachments::AttachmentDownloadAssembler moved;
        {
            attachments::AttachmentDownloadAssembler original;
            QVERIFY(original.begin(path, key, twoChunkContext()));
            QVERIFY(original.acceptChunk(0, encrypted[0], sha256Hex(encrypted[0]), false));
            moved = std::move(original);
        }
        QVERIFY(QFile::exists(partPath(path)));
    }
    QVERIFY(!QFile::exists(path));
    QVERIFY(!QFile::exists(partPath(path)));
}

QTEST_APPLESS_MAIN(AttachmentDownloadAssemblerTests)

#include "attachment_download_assembler_tests.moc"
