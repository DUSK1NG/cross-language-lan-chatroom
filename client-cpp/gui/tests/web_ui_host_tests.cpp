#include "web_ui_host.hpp"

#include <QTest>

class WebUiHostTests final : public QObject {
    Q_OBJECT

private slots:
    void acceptsLoopbackDevelopmentUrl();
    void rejectsNonLoopbackDevelopmentUrl();
    void exposesFixedReleaseUrl();
    void opensOnlyPlainWebLinksExternally();
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

QTEST_APPLESS_MAIN(WebUiHostTests)
#include "web_ui_host_tests.moc"
