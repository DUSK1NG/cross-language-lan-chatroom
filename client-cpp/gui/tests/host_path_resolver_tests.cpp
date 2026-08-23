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
    void marksMemberPackageWithoutServerAsUnavailable();
    void findsHostFilesFromDevelopmentBuildDirectory();
    void findsHostFilesFromUnifiedOutputDirectory();
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

void HostPathResolverTests::marksMemberPackageWithoutServerAsUnavailable() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString cert = QDir(temp.path()).filePath("server-go/certs/server-lan.crt");
    QVERIFY(QDir().mkpath(QFileInfo(cert).absolutePath()));
    QVERIFY(QFile(cert).open(QIODevice::WriteOnly));

    const HostPathResolver::HostPaths paths = HostPathResolver::resolveHostPaths(temp.path());

    QCOMPARE(paths.certFile, QFileInfo(cert).absoluteFilePath());
    QVERIFY(!paths.available());
}

void HostPathResolverTests::findsHostFilesFromDevelopmentBuildDirectory() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QDir root(temp.path());
    const QString buildDir = root.filePath("client-cpp/gui/build-test");
    const QString server = root.filePath("server-go/chat-server.exe");
    const QString cert = root.filePath("server-go/certs/server-lan.crt");
    const QString key = root.filePath("server-go/certs/server-lan.key");
    QVERIFY(QDir().mkpath(buildDir));
    QVERIFY(QDir().mkpath(QFileInfo(key).absolutePath()));
    QVERIFY(QFile(server).open(QIODevice::WriteOnly));
    QVERIFY(QFile(cert).open(QIODevice::WriteOnly));
    QVERIFY(QFile(key).open(QIODevice::WriteOnly));

    const HostPathResolver::HostPaths paths = HostPathResolver::resolveHostPaths(buildDir);

    QCOMPARE(paths.serverExe, QFileInfo(server).absoluteFilePath());
    QCOMPARE(paths.certFile, QFileInfo(cert).absoluteFilePath());
    QCOMPARE(paths.keyFile, QFileInfo(key).absoluteFilePath());
    QVERIFY(paths.available());
}

void HostPathResolverTests::findsHostFilesFromUnifiedOutputDirectory() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QDir root(temp.path());
    const QString buildDir = root.filePath("out/modern-msvc-x64");
    const QString server = root.filePath("server-go/chat-server.exe");
    const QString cert = root.filePath("server-go/certs/server-lan.crt");
    const QString key = root.filePath("server-go/certs/server-lan.key");
    QVERIFY(QDir().mkpath(buildDir));
    QVERIFY(QDir().mkpath(QFileInfo(key).absolutePath()));
    QVERIFY(QFile(server).open(QIODevice::WriteOnly));
    QVERIFY(QFile(cert).open(QIODevice::WriteOnly));
    QVERIFY(QFile(key).open(QIODevice::WriteOnly));

    const HostPathResolver::HostPaths paths = HostPathResolver::resolveHostPaths(buildDir);

    QCOMPARE(paths.serverExe, QFileInfo(server).absoluteFilePath());
    QCOMPARE(paths.certFile, QFileInfo(cert).absoluteFilePath());
    QCOMPARE(paths.keyFile, QFileInfo(key).absoluteFilePath());
    QVERIFY(paths.available());
}

QTEST_MAIN(HostPathResolverTests)
#include "host_path_resolver_tests.moc"
