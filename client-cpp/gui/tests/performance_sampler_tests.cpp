#include "performance_sampler.hpp"

#include <QtTest>

class PerformanceSamplerTests final : public QObject {
    Q_OBJECT

private slots:
    void computesFrameMetrics();
    void keepsBoundedSampleWindow();
};

void PerformanceSamplerTests::computesFrameMetrics() {
    PerformanceSampler sampler;
    sampler.recordFrameIntervalForTesting(10'000'000);
    sampler.recordFrameIntervalForTesting(20'000'000);
    sampler.recordFrameIntervalForTesting(30'000'000);
    sampler.recordFrameIntervalForTesting(40'000'000);
    sampler.recordFrameIntervalForTesting(50'000'000);

    QCOMPARE(sampler.sampleCount(), 5);
    QCOMPARE(sampler.p95FrameMs(), 50.0);
    QCOMPARE(sampler.p99FrameMs(), 50.0);
    QCOMPARE(sampler.maxFrameMs(), 50.0);
    QVERIFY(sampler.fps() > 0.0);
}

void PerformanceSamplerTests::keepsBoundedSampleWindow() {
    PerformanceSampler sampler;
    for (int index = 0; index < 300; ++index) {
        sampler.recordFrameIntervalForTesting(16'000'000);
    }

    QCOMPARE(sampler.sampleCount(), 240);
    QVERIFY(sampler.fps() > 60.0 - 0.1);
    QCOMPARE(sampler.maxFrameMs(), 16.0);
}

QTEST_MAIN(PerformanceSamplerTests)

#include "performance_sampler_tests.moc"
