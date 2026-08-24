#include <QtTest/QtTest>

#include "device_identity.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

class DeviceIdentityTests final : public QObject {
    Q_OBJECT

private slots:
    void storesAStablePerCertificateCredential();
};

void DeviceIdentityTests::storesAStablePerCertificateCredential() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = QDir(temporary.path()).filePath("device-store");
    const QString certificate = QDir(temporary.path()).filePath("server-lan.crt");
    QFile certificateFile(certificate);
    QVERIFY(certificateFile.open(QIODevice::WriteOnly));
    QVERIFY(certificateFile.write("public test certificate") > 0);
    certificateFile.close();

    qputenv("LAN_CHAT_TEST_DEVICE_DATA_ROOT", root.toUtf8());
    QString first;
    QString second;
    QString error;
    QVERIFY2(DeviceIdentity::loadOrCreate(certificate, &first, &error), qPrintable(error));
    QVERIFY2(DeviceIdentity::loadOrCreate(certificate, &second, &error), qPrintable(error));
    qunsetenv("LAN_CHAT_TEST_DEVICE_DATA_ROOT");

    QVERIFY(!first.isEmpty());
    QCOMPARE(first, second);
    QVERIFY(QDir(root).entryList({"*.bin"}, QDir::Files).size() == 1);
}

QTEST_MAIN(DeviceIdentityTests)
#include "device_identity_tests.moc"
