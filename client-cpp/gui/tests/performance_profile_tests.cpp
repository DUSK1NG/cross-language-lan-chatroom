#include "performance_profile.hpp"

#include <QtTest>

class PerformanceProfileTests final : public QObject {
    Q_OBJECT

private slots:
    void explicitModesExposeStableCapabilities();
    void invalidModeFallsBackToAutomatic();
    void automaticModeUsesGraphicsContext();
    void automaticModeRespondsToSlowFrames();
    void automaticModeReducesMotionWhenP95Exceeds60FpsBudget();
    void frameObservationWindowStaysBounded();
    void automaticModeExposesObservedMetrics();
    void automaticModeConfirmsFrameCandidatesBeforeSwitching();
    void detachWindowCanRunBeforeWindowDestruction();
};

void PerformanceProfileTests::explicitModesExposeStableCapabilities() {
    PerformanceProfile profile;

    profile.setMode(QStringLiteral("High"));
    QCOMPARE(profile.effectiveMode(), QStringLiteral("High"));
    QVERIFY(profile.effectsEnabled());
    QVERIFY(profile.animationsEnabled());
    QVERIFY(profile.gradientsEnabled());
    QCOMPARE(profile.animationDurationScale(), 1.0);

    profile.setMode(QStringLiteral("Balanced"));
    QCOMPARE(profile.effectiveMode(), QStringLiteral("Balanced"));
    QVERIFY(profile.effectsEnabled());
    QVERIFY(profile.animationsEnabled());
    QVERIFY(profile.gradientsEnabled());
    QCOMPARE(profile.animationDurationScale(), 0.75);

    profile.setMode(QStringLiteral("Power Saving"));
    QCOMPARE(profile.effectiveMode(), QStringLiteral("Power Saving"));
    QVERIFY(!profile.effectsEnabled());
    QVERIFY(!profile.animationsEnabled());
    QVERIFY(!profile.gradientsEnabled());
    QCOMPARE(profile.animationDurationScale(), 0.0);
}

void PerformanceProfileTests::invalidModeFallsBackToAutomatic() {
    PerformanceProfile profile;
    profile.setMode(QStringLiteral("not-a-mode"));

    QCOMPARE(profile.mode(), QStringLiteral("Automatic"));
}

void PerformanceProfileTests::automaticModeUsesGraphicsContext() {
    PerformanceProfile profile;
    profile.setMode(QStringLiteral("Automatic"));

    profile.updateGraphicsContext(false, true, 60.0);
    QCOMPARE(profile.effectiveMode(), QStringLiteral("Power Saving"));

    profile.updateGraphicsContext(true, false, 60.0);
    QCOMPARE(profile.effectiveMode(), QStringLiteral("Balanced"));

    profile.updateGraphicsContext(true, false, 144.0);
    QCOMPARE(profile.effectiveMode(), QStringLiteral("High"));
}

void PerformanceProfileTests::automaticModeRespondsToSlowFrames() {
    PerformanceProfile profile;
    profile.setMode(QStringLiteral("Automatic"));
    profile.updateGraphicsContext(true, false, 144.0);
    QCOMPARE(profile.effectiveMode(), QStringLiteral("High"));

    for (int index = 0; index < 24; ++index) profile.observeFrameTime(30.0);
    QCOMPARE(profile.effectiveMode(), QStringLiteral("High"));
    profile.observeFrameTime(30.0);
    profile.observeFrameTime(30.0);
    QCOMPARE(profile.effectiveMode(), QStringLiteral("Balanced"));

    for (int index = 0; index < 24; ++index) profile.observeFrameTime(45.0);
    QCOMPARE(profile.effectiveMode(), QStringLiteral("Power Saving"));
}

void PerformanceProfileTests::automaticModeReducesMotionWhenP95Exceeds60FpsBudget() {
    PerformanceProfile profile;
    profile.setMode(QStringLiteral("Automatic"));
    profile.updateGraphicsContext(true, false, 144.0);

    for (int index = 0; index < 24; ++index) profile.observeFrameTime(17.0);
    profile.observeFrameTime(17.0);
    profile.observeFrameTime(17.0);

    QCOMPARE(profile.effectiveMode(), QStringLiteral("Balanced"));
    QCOMPARE(profile.automaticReason(), QStringLiteral("p95-over-16.7ms"));
    QCOMPARE(profile.animationDurationScale(), 0.75);
}

void PerformanceProfileTests::frameObservationWindowStaysBounded() {
    PerformanceProfile profile;
    profile.setMode(QStringLiteral("Automatic"));
    profile.updateGraphicsContext(true, false, 144.0);

    for (int index = 0; index < 100; ++index) profile.observeFrameTime(16.0);

    QCOMPARE(profile.observedFrameCount(), 24);
}

void PerformanceProfileTests::automaticModeExposesObservedMetrics() {
    PerformanceProfile profile;
    profile.setMode(QStringLiteral("Automatic"));
    profile.updateGraphicsContext(true, false, 144.0);

    for (int index = 0; index < 20; ++index) profile.observeFrameTime(16.0);
    for (int index = 0; index < 4; ++index) profile.observeFrameTime(32.0);

    QCOMPARE(profile.observedFrameCount(), 24);
    QCOMPARE(profile.observedP95FrameMs(), 32.0);
    QCOMPARE(profile.observedMaxFrameMs(), 32.0);
    QVERIFY(profile.observedFps() > 50.0);
    QVERIFY(profile.observedFps() < 60.0);
    QCOMPARE(profile.automaticReason(), QStringLiteral("p95-over-16.7ms"));
}

void PerformanceProfileTests::automaticModeConfirmsFrameCandidatesBeforeSwitching() {
    PerformanceProfile profile;
    profile.setMode(QStringLiteral("Automatic"));
    profile.updateGraphicsContext(true, false, 144.0);
    QCOMPARE(profile.effectiveMode(), QStringLiteral("High"));

    for (int index = 0; index < 24; ++index) profile.observeFrameTime(30.0);
    QCOMPARE(profile.effectiveMode(), QStringLiteral("High"));
    profile.observeFrameTime(30.0);
    QCOMPARE(profile.effectiveMode(), QStringLiteral("High"));
    profile.observeFrameTime(30.0);
    QCOMPARE(profile.effectiveMode(), QStringLiteral("Balanced"));

    for (int index = 0; index < 24; ++index) profile.observeFrameTime(16.0);
    QCOMPARE(profile.effectiveMode(), QStringLiteral("Balanced"));
    profile.observeFrameTime(16.0);
    QCOMPARE(profile.effectiveMode(), QStringLiteral("High"));
}

void PerformanceProfileTests::detachWindowCanRunBeforeWindowDestruction() {
    PerformanceProfile profile;
    profile.attachWindow(nullptr);
    profile.detachWindow();

    QCOMPARE(profile.observedFrameCount(), 0);
}

QTEST_GUILESS_MAIN(PerformanceProfileTests)

#include "performance_profile_tests.moc"
