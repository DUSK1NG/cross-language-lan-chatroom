#include "host_path_resolver.hpp"
#include "local_host_bootstrap.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

class LocalHostBootstrapTests final : public QObject {
    Q_OBJECT

private slots:
    void initializesCertificateKeyAndDatabaseWithoutListening();
};

void LocalHostBootstrapTests::initializesCertificateKeyAndDatabaseWithoutListening() {
    const HostPathResolver::HostPaths bundled =
        HostPathResolver::resolveHostPaths(QCoreApplication::applicationDirPath());
    if (!QFileInfo::exists(bundled.serverExe)) {
        QSKIP("Built Go server is not available beside this test executable");
    }

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QDir root(temp.path());
    const HostPathResolver::HostPaths paths{
        bundled.serverExe,
        root.filePath("certs/server-lan.crt"),
        root.filePath("certs/server-lan.key"),
        root.filePath("state/chat.db")};

    const LocalHostBootstrap::Result first = LocalHostBootstrap::ensureInitialized(paths);
    QVERIFY2(first.ready, qPrintable(first.error));
    QVERIFY(QFileInfo::exists(paths.certFile));
    QVERIFY(QFileInfo::exists(paths.keyFile));
    QVERIFY(QFileInfo::exists(paths.dbFile));

    const LocalHostBootstrap::Result second = LocalHostBootstrap::ensureInitialized(paths);
    QVERIFY2(second.ready, qPrintable(second.error));
}

QTEST_MAIN(LocalHostBootstrapTests)
#include "local_host_bootstrap_tests.moc"
