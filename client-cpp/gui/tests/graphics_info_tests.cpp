#include "graphics_info.hpp"

#include <QtTest>

class GraphicsInfoTests final : public QObject {
    Q_OBJECT

private slots:
    void exposesSafeDefaultsWithoutWindow();
    void refreshIsSafeAndIdempotentWithoutWindow();
};

void GraphicsInfoTests::exposesSafeDefaultsWithoutWindow() {
    GraphicsInfo info;

    info.refresh();

    QCOMPARE(info.graphicsApi(), QStringLiteral("Unknown"));
    QCOMPARE(info.renderer(), QStringLiteral("Unknown"));
    QCOMPARE(info.vendor(), QStringLiteral("Unknown"));
    QCOMPARE(info.resolution(), QStringLiteral("Unknown"));
    QVERIFY(!info.hardwareAcceleration());
    QVERIFY(!info.softwareRendering());
    QCOMPARE(info.refreshRate(), 0.0);
    QCOMPARE(info.dpi(), 0.0);
}

void GraphicsInfoTests::refreshIsSafeAndIdempotentWithoutWindow() {
    GraphicsInfo info;

    info.refresh();
    const QString firstApi = info.graphicsApi();
    const QString firstResolution = info.resolution();

    info.refresh();

    QCOMPARE(info.graphicsApi(), firstApi);
    QCOMPARE(info.resolution(), firstResolution);
}

QTEST_GUILESS_MAIN(GraphicsInfoTests)

#include "graphics_info_tests.moc"
