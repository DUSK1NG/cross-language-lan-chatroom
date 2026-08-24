#include <QtTest/QtTest>

#include "host_path_resolver.hpp"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

class HostPathResolverTests final : public QObject {
    Q_OBJECT

private slots:
    void prefersCertificateSiblingKey();
    void findsBundledServerKey();
    void returnsEmptyWhenNoCandidateExists();
    void marksMemberPackageWithoutServerAsUnavailable();
    void marksBundledServerAsAvailableBeforeFirstCertificate();
    void findsHostFilesFromDevelopmentBuildDirectory();
    void findsHostFilesFromUnifiedOutputDirectory();
    void usesPerUserAppDataForNewHostIdentity();
    void usesExplicitTestDataRootWhenProvided();
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

void HostPathResolverTests::marksBundledServerAsAvailableBeforeFirstCertificate() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString server = QDir(temp.path()).filePath("server-go/chat-server.exe");
    QVERIFY(QDir().mkpath(QFileInfo(server).absolutePath()));
    QVERIFY(QFile(server).open(QIODevice::WriteOnly));

    const HostPathResolver::HostPaths paths = HostPathResolver::resolveHostPaths(temp.path());

    QCOMPARE(paths.serverExe, QFileInfo(server).absoluteFilePath());
    QVERIFY(!QFileInfo::exists(paths.certFile));
    QVERIFY(!QFileInfo::exists(paths.keyFile));
    QVERIFY(paths.available());
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

void HostPathResolverTests::usesPerUserAppDataForNewHostIdentity() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString server = QDir(temp.path()).filePath("server-go/chat-server.exe");
    QVERIFY(QDir().mkpath(QFileInfo(server).absolutePath()));
    QVERIFY(QFile(server).open(QIODevice::WriteOnly));

    const HostPathResolver::HostPaths paths = HostPathResolver::resolveHostPaths(temp.path());
    const QString localData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString expectedRoot = QDir::cleanPath(
        QDir(localData).filePath(QStringLiteral("DUSK1NG/LAN Chat/host")));

    QCOMPARE(paths.serverExe, QFileInfo(server).absoluteFilePath());
    QVERIFY(QDir::cleanPath(paths.certFile).startsWith(expectedRoot + QLatin1Char('/')));
    QVERIFY(QDir::cleanPath(paths.keyFile).startsWith(expectedRoot + QLatin1Char('/')));
    QVERIFY(QDir::cleanPath(paths.dbFile).startsWith(expectedRoot + QLatin1Char('/')));
    QVERIFY(!paths.certFile.startsWith(temp.path()));
}

void HostPathResolverTests::usesExplicitTestDataRootWhenProvided() {
    QTemporaryDir packageRoot;
    QTemporaryDir hostDataRoot;
    QVERIFY(packageRoot.isValid());
    QVERIFY(hostDataRoot.isValid());
    const QString server = QDir(packageRoot.path()).filePath("server-go/chat-server.exe");
    QVERIFY(QDir().mkpath(QFileInfo(server).absolutePath()));
    QVERIFY(QFile(server).open(QIODevice::WriteOnly));

    constexpr auto variableName = "LAN_CHAT_TEST_HOST_DATA_ROOT";
    const bool hadPreviousValue = qEnvironmentVariableIsSet(variableName);
    const QByteArray previousValue = qgetenv(variableName);
    qputenv(variableName, hostDataRoot.path().toUtf8());

    const HostPathResolver::HostPaths paths = HostPathResolver::resolveHostPaths(packageRoot.path());

    if (hadPreviousValue) {
        qputenv(variableName, previousValue);
    } else {
        qunsetenv(variableName);
    }

    const QString expectedRoot = QDir::cleanPath(hostDataRoot.path());
    QCOMPARE(paths.serverExe, QFileInfo(server).absoluteFilePath());
    QVERIFY(QDir::cleanPath(paths.certFile).startsWith(expectedRoot + QLatin1Char('/')));
    QVERIFY(QDir::cleanPath(paths.keyFile).startsWith(expectedRoot + QLatin1Char('/')));
    QVERIFY(QDir::cleanPath(paths.dbFile).startsWith(expectedRoot + QLatin1Char('/')));
}

QTEST_MAIN(HostPathResolverTests)
#include "host_path_resolver_tests.moc"
