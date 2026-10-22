#include "web_ui_host.hpp"

#include <QTest>
#include <QApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QWebEngineProfile>

class WebUiHostTests final : public QObject {
    Q_OBJECT

private slots:
    void acceptsLoopbackDevelopmentUrl();
    void rejectsNonLoopbackDevelopmentUrl();
    void exposesFixedReleaseUrl();
    void opensOnlyPlainWebLinksExternally();
    void persistsReleaseSeenAcrossRestarts();
};

void WebUiHostTests::acceptsLoopbackDevelopmentUrl() {
    QVERIFY(WebUiHost::isDevelopmentUrlAllowed(QUrl("http://127.0.0.1:5173")));
    QVERIFY(WebUiHost::isDevelopmentUrlAllowed(QUrl("http://localhost:5173")));
    QVERIFY(WebUiHost::isDevelopmentUrlAllowed(QUrl("http://[::1]:5173")));
}

void WebUiHostTests::rejectsNonLoopbackDevelopmentUrl() {
    QVERIFY(!WebUiHost::isDevelopmentUrlAllowed(QUrl("http://192.168.1.20:5173")));
    QVERIFY(!WebUiHost::isDevelopmentUrlAllowed(QUrl("https://example.com")));
    QVERIFY(!WebUiHost::isDevelopmentUrlAllowed(QUrl("qrc:/frontend/index.html")));
}

void WebUiHostTests::exposesFixedReleaseUrl() {
    QCOMPARE(WebUiHost::releaseUrl(), QUrl("qrc:/frontend/index.html"));
}

void WebUiHostTests::opensOnlyPlainWebLinksExternally() {
    QVERIFY(WebUiHost::opensExternally(QUrl("https://example.com/docs")));
    QVERIFY(WebUiHost::opensExternally(QUrl("http://192.168.1.20:8080/")));
    QVERIFY(!WebUiHost::opensExternally(QUrl("qrc:/frontend/index.html")));
    QVERIFY(!WebUiHost::opensExternally(QUrl("file:///C:/Windows/system32/calc.exe")));
    QVERIFY(!WebUiHost::opensExternally(QUrl("javascript:alert(1)")));
    QVERIFY(!WebUiHost::opensExternally(QUrl("https://user:secret@example.com/")));
    QVERIFY(!WebUiHost::opensExternally(QUrl("https://")));
}

void WebUiHostTests::persistsReleaseSeenAcrossRestarts() {
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    for (const auto& mode : {QStringLiteral("write"), QStringLiteral("read")}) {
        QProcess process;
        process.start(QCoreApplication::applicationFilePath(),
                      {QStringLiteral("--release-storage-probe"), mode, storage.path()});
        QVERIFY2(process.waitForFinished(30000), qPrintable(process.errorString()));
        const auto output = mode.toUtf8() + ": exit " + QByteArray::number(process.exitCode())
            + '\n' + process.readAllStandardOutput() + process.readAllStandardError();
        QVERIFY2(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
                 output.constData());
    }
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() == 4 && arguments.at(1) == QStringLiteral("--release-storage-probe")) {
        QWebEngineProfile profile(QStringLiteral("LANChat"));
        profile.setPersistentStoragePath(arguments.at(3) + QStringLiteral("/storage"));
        profile.setCachePath(arguments.at(3) + QStringLiteral("/cache"));
        ExternalLinkPage page(&profile);
        QObject::connect(&page, &QWebEnginePage::loadFinished, &app, [&](bool ok) {
            if (!ok) {
                app.exit(1);
                return;
            }
            const auto script = arguments.at(2) == QStringLiteral("write")
                ? QStringLiteral("localStorage.setItem('lan-chat.releaseSeen', 'test-release'); localStorage.getItem('lan-chat.releaseSeen')")
                : QStringLiteral("localStorage.getItem('lan-chat.releaseSeen')");
            page.runJavaScript(script, [&](const QVariant& result) {
                app.exit(result.toString() == QStringLiteral("test-release") ? 0 : 1);
            });
        });
        page.setHtml(QStringLiteral("<!doctype html><html></html>"), WebUiHost::releaseUrl());
        return app.exec();
    }
    WebUiHostTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "web_ui_host_tests.moc"
