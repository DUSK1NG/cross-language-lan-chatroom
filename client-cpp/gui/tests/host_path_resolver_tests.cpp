#include <QtTest/QtTest>

#include "host_path_resolver.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

class HostPathResolverTests final : public QObject {
    Q_OBJECT

private slots:
    void prefersCertificateSiblingKey();
    void findsBundledServerKey();
    void returnsEmptyWhenNoCandidateExists();
};

void HostPathResolverTests::prefersCertificateSiblingKey() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString cert = QDir(temp.path()).filePath("certs/server-lan.crt");
    const QString key = QDir(temp.path()).filePath("certs/server-lan.key");
    QVERIFY(QDir().mkpath(QFileInfo(cert).absolutePath()));
    QVERIFY(QFile(key).open(QIODevice::WriteOnly));

    QCOMPARE(HostPathResolver::findPrivateKeyPath({}, cert), QFileInfo(key).absoluteFilePath());
}

void HostPathResolverTests::findsBundledServerKey() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString server = QDir(temp.path()).filePath("server-go/chat-server.exe");
    const QString key = QDir(temp.path()).filePath("server-go/certs/server-lan.key");
    QVERIFY(QDir().mkpath(QFileInfo(key).absolutePath()));
    QVERIFY(QFile(key).open(QIODevice::WriteOnly));

    QCOMPARE(HostPathResolver::findPrivateKeyPath(server, {}), QFileInfo(key).absoluteFilePath());
}

void HostPathResolverTests::returnsEmptyWhenNoCandidateExists() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QVERIFY(HostPathResolver::findPrivateKeyPath(
                QDir(temp.path()).filePath("chat-server.exe"),
                QDir(temp.path()).filePath("server.crt"))
                .isEmpty());
}

QTEST_MAIN(HostPathResolverTests)
#include "host_path_resolver_tests.moc"
