#include "network_diagnostics.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class NetworkDiagnosticsTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void writesOnlyConnectionMetadataWhenEnabled();
    void persistsAnExplicitUserChoiceWhenNoEnvironmentOverrideExists();
    void cleanup();
};

void NetworkDiagnosticsTests::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("LANChatTests"));
    QCoreApplication::setApplicationName(QStringLiteral("NetworkDiagnosticsTests"));
    NetworkDiagnostics::setEnabled(false);
}

void NetworkDiagnosticsTests::writesOnlyConnectionMetadataWhenEnabled() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    qputenv("LAN_CHAT_DIAGNOSTICS", "1");
    qputenv("LAN_CHAT_DIAGNOSTICS_DIR", directory.path().toUtf8());

    NetworkDiagnostics::writeConnectionEvent(
        QStringLiteral("connection_lost"), QStringLiteral("frp-bus.com"), 50440, 3,
        QStringLiteral("read frame: EOF"));

    const QString path = NetworkDiagnostics::sessionLogFilePath();
    QVERIFY2(!path.isEmpty(), "diagnostics must expose the active log path");
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));
    const QString text = QString::fromUtf8(file.readAll());
    QVERIFY(text.contains(QStringLiteral("event=connection_lost")));
    QVERIFY(text.contains(QStringLiteral("endpoint=frp-bus.com:50440")));
    QVERIFY(text.contains(QStringLiteral("attempt=3")));
    QVERIFY(text.contains(QStringLiteral("reason=read frame: EOF")));
    QVERIFY(!text.contains(QStringLiteral("content=")));

    qunsetenv("LAN_CHAT_DIAGNOSTICS");
    qunsetenv("LAN_CHAT_DIAGNOSTICS_DIR");
}

void NetworkDiagnosticsTests::persistsAnExplicitUserChoiceWhenNoEnvironmentOverrideExists() {
    qunsetenv("LAN_CHAT_DIAGNOSTICS");

    NetworkDiagnostics::setEnabled(true);
    QVERIFY(NetworkDiagnostics::enabled());

    NetworkDiagnostics::setEnabled(false);
    QVERIFY(!NetworkDiagnostics::enabled());
}

void NetworkDiagnosticsTests::cleanup() {
    NetworkDiagnostics::setEnabled(false);
    qunsetenv("LAN_CHAT_DIAGNOSTICS");
    qunsetenv("LAN_CHAT_DIAGNOSTICS_DIR");
}

QTEST_GUILESS_MAIN(NetworkDiagnosticsTests)

#include "network_diagnostics_tests.moc"
