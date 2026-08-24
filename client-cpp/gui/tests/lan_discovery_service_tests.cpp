#include "lan_discovery_service.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class LanDiscoveryServiceTests final : public QObject {
    Q_OBJECT

private slots:
    void acceptsMatchingPublicCertificateAnnouncement();
    void rejectsMismatchedFingerprint();
    void writesOnlyThePublicCertificateForSelectedHost();
};

namespace {
QByteArray certificate() {
    return QByteArrayLiteral(
        "-----BEGIN CERTIFICATE-----\n"
        "MIIDGzCCAgOgAwIBAgIUHHbWjw/fauIAEC6cdc7j8aAk3pEwDQYJKoZIhvcNAQEL\n"
        "BQAwHTEbMBkGA1UEAwwSbGFuLWRpc2NvdmVyeS10ZXN0MB4XDTI2MDgyNDAwMzIx\n"
        "NloXDTI2MDgyNTAwMzIxNlowHTEbMBkGA1UEAwwSbGFuLWRpc2NvdmVyeS10ZXN0\n"
        "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEApD7Yzu8zutlo1tSQs2Lf\n"
        "Fqf+jwOu6eqZSkgFTAqPtks5wltaYJ32ZP/ohuWbKzZ64qJXx+EdQUkDg8t05WB2\n"
        "BmBHFe1CeplU4gXPaA77y33J013/qa0cRMeCZBngP7ayOTp7pGoTdSJffR9SufSP\n"
        "4UeUSewmrN82eqN/WfWoz/csi4nQ432bmKA6NfJdpgb73O84LRMXFeDicz6sNsNy\n"
        "TdYIwo7LDjLr7ZT5dHhhJiEj4pf33BkOSGbdsP5PVPJp6odONPoUJzyXVWnVF9iM\n"
        "Eonlb7H7sQwVw8nWqsl8K4HvyxvBI5r/cN9+Zd/izWmJnURHqJRnZMs05/QS4UnP\n"
        "twIDAQABo1MwUTAdBgNVHQ4EFgQU+47+eybR83ltYMS4o/0f+MgoKhYwHwYDVR0j\n"
        "BBgwFoAU+47+eybR83ltYMS4o/0f+MgoKhYwDwYDVR0TAQH/BAUwAwEB/zANBgkq\n"
        "hkiG9w0BAQsFAAOCAQEAHeZhKQoAAo6Q0wIt0Wo0eZvH60EhT8pTDToiyFnSPhDo\n"
        "J2wgc+gwAe/zPjs5npahUUDe2n0rXApa9G/cPW9J7qPRcF6v0+131SxEGLqf5jAj\n"
        "HhAgJx56HQfuB/r8ZD/5tM+vCZG2aMDwjFIEsU8WUpNwOGz5km1edjaXABb3OiIl\n"
        "6+oiUdqD/Y0QrNQ2BT7tKA26wniQPlmzuI1zg4ItAKbTI+hmHDvmxUDEqWH6mXVh\n"
        "5aGcCZIjZnp6eUYw/HHwB92BPVU7KMrRVnVZynUmHfZOekvioufws4Ac1ay2L5Zs\n"
        "SG0LQLgbxh8BloRWF5d0zK7simah/3GdRAcXn3XeQQ==\n"
        "-----END CERTIFICATE-----\n");
}

QByteArray announcement(const QString& fingerprint) {
    return QJsonDocument(QJsonObject{{"service", "lan-chat"}, {"version", 1},
                                     {"hostName", "Alice PC"}, {"port", 8888},
                                     {"certificateBase64", QString::fromLatin1(certificate().toBase64())},
                                     {"fingerprintSha256", fingerprint}}).toJson(QJsonDocument::Compact);
}
}

void LanDiscoveryServiceTests::acceptsMatchingPublicCertificateAnnouncement() {
    const QString fingerprint = QString::fromLatin1(
        QCryptographicHash::hash(certificate(), QCryptographicHash::Sha256).toHex());
    LanDiscoveredHost host;
    QVERIFY(LanDiscoveryService::decodeAnnouncement(announcement(fingerprint),
                                                    QHostAddress(QStringLiteral("192.168.8.23")),
                                                    QDateTime::currentDateTimeUtc(), &host));
    QCOMPARE(host.hostName, QStringLiteral("Alice PC"));
    QCOMPARE(host.serverIp, QStringLiteral("192.168.8.23"));
    QCOMPARE(host.id, fingerprint);
}

void LanDiscoveryServiceTests::rejectsMismatchedFingerprint() {
    LanDiscoveredHost host;
    QVERIFY(!LanDiscoveryService::decodeAnnouncement(announcement(QString(64, QLatin1Char('0'))),
                                                     QHostAddress(QStringLiteral("192.168.8.23")),
                                                     QDateTime::currentDateTimeUtc(), &host));
}

void LanDiscoveryServiceTests::writesOnlyThePublicCertificateForSelectedHost() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    LanDiscoveryService service(nullptr, temporary.path());
    LanDiscoveredHost host;
    const QString fingerprint = QString::fromLatin1(
        QCryptographicHash::hash(certificate(), QCryptographicHash::Sha256).toHex());
    QVERIFY(LanDiscoveryService::decodeAnnouncement(announcement(fingerprint),
                                                    QHostAddress(QStringLiteral("192.168.8.23")),
                                                    QDateTime::currentDateTimeUtc(), &host));
    QString error;
    const QString path = service.persistCertificate(host, &error);
    QVERIFY2(!path.isEmpty(), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), certificate());
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".key")));
}

QTEST_APPLESS_MAIN(LanDiscoveryServiceTests)

#include "lan_discovery_service_tests.moc"
