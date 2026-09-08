#include "attachments/attachment_manifest.hpp"

#include <QtTest/QtTest>

class AttachmentManifestTests : public QObject {
    Q_OBJECT

private slots:
    void roundTrip();
    void rejectsWrongKeySize();
    void rejectsMissingRequiredFields();
    void derivesStableChatMessageId();
};

void AttachmentManifestTests::roundTrip() {
    attachments::AttachmentManifest expected;
    expected.attachmentId = QStringLiteral("attachment-1");
    expected.fileName = QStringLiteral("秘密.txt");
    expected.room = QStringLiteral("lobby");
    expected.groupId = QStringLiteral("group-1");
    expected.logicalSize = 123456;
    expected.chunkSize = 48128;
    expected.epoch = 7;
    expected.key = QByteArray(32, '\x2a');

    const QByteArray encoded = expected.encode();
    QVERIFY(!encoded.isEmpty());
    attachments::AttachmentManifest actual;
    QVERIFY(attachments::AttachmentManifest::decode(encoded, actual));
    QCOMPARE(actual.attachmentId, expected.attachmentId);
    QCOMPARE(actual.fileName, expected.fileName);
    QCOMPARE(actual.room, expected.room);
    QCOMPARE(actual.groupId, expected.groupId);
    QCOMPARE(actual.logicalSize, expected.logicalSize);
    QCOMPARE(actual.chunkSize, expected.chunkSize);
    QCOMPARE(actual.epoch, expected.epoch);
    QCOMPARE(actual.key, expected.key);
}

void AttachmentManifestTests::rejectsWrongKeySize() {
    attachments::AttachmentManifest manifest;
    manifest.attachmentId = QStringLiteral("attachment-1");
    manifest.fileName = QStringLiteral("file.bin");
    manifest.room = QStringLiteral("lobby");
    manifest.groupId = QStringLiteral("group-1");
    manifest.logicalSize = 1;
    manifest.chunkSize = 48128;
    manifest.key = QByteArray(31, '\x01');
    QVERIFY(manifest.encode().isEmpty());
}

void AttachmentManifestTests::rejectsMissingRequiredFields() {
    attachments::AttachmentManifest actual;
    QVERIFY(!attachments::AttachmentManifest::decode(
        QByteArrayLiteral("{\"attachment_id\":\"a\"}"), actual));
}

void AttachmentManifestTests::derivesStableChatMessageId() {
    const QString first = attachments::manifestMessageId(QStringLiteral("attachment-1"));
    QCOMPARE(first, attachments::manifestMessageId(QStringLiteral("attachment-1")));
    QVERIFY(first.startsWith(QStringLiteral("am-")));
    QCOMPARE(first.size(), 59);
    QVERIFY(first != attachments::manifestMessageId(QStringLiteral("attachment-2")));
}

QTEST_APPLESS_MAIN(AttachmentManifestTests)

#include "attachment_manifest_tests.moc"
