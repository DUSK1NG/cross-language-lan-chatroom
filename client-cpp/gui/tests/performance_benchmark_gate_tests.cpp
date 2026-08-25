#include "performance_benchmark_gate.hpp"

#include <QtTest>

class PerformanceBenchmarkGateTests final : public QObject {
    Q_OBJECT

private slots:
    void passesReferenceProfileWithinBudgets();
    void rejectsUnsupportedBenchmarkEnvironment();
    void rejectsBatterySaverBenchmarkEnvironment();
    void reportsEveryExceededBudget();
};

void PerformanceBenchmarkGateTests::passesReferenceProfileWithinBudgets() {
    const PerformanceBenchmarkEnvironment environment{true, false, 120.0};
    const PerformanceBenchmarkMeasurements measurements{
        100LL * 1024 * 1024, 115LL * 1024 * 1024, 16.7, 20.0, 1};

    const PerformanceBenchmarkVerdict verdict =
        PerformanceBenchmarkGate::evaluate(environment, measurements);

    QVERIFY(verdict.supported);
    QVERIFY(verdict.passed);
    QVERIFY(qAbs(verdict.workingSetGrowthPercent - 15.0) < 0.001);
    QVERIFY(verdict.failures.isEmpty());
}

void PerformanceBenchmarkGateTests::rejectsUnsupportedBenchmarkEnvironment() {
    const PerformanceBenchmarkEnvironment environment{false, true, 59.0};
    const PerformanceBenchmarkMeasurements measurements{
        100LL * 1024 * 1024, 100LL * 1024 * 1024, 10.0, 12.0, 0};

    const PerformanceBenchmarkVerdict verdict =
        PerformanceBenchmarkGate::evaluate(environment, measurements);

    QVERIFY(!verdict.supported);
    QVERIFY(!verdict.passed);
    QCOMPARE(verdict.unsupportedReasons,
             QStringList({QStringLiteral("software-rendering"),
                          QStringLiteral("no-hardware-acceleration"),
                          QStringLiteral("refresh-rate-below-60hz")}));
}

void PerformanceBenchmarkGateTests::rejectsBatterySaverBenchmarkEnvironment() {
    const PerformanceBenchmarkEnvironment environment{true, false, 144.0, true};
    const PerformanceBenchmarkMeasurements measurements{
        100LL * 1024 * 1024, 100LL * 1024 * 1024, 10.0, 12.0, 0};

    const PerformanceBenchmarkVerdict verdict =
        PerformanceBenchmarkGate::evaluate(environment, measurements);

    QVERIFY(!verdict.supported);
    QVERIFY(!verdict.passed);
    QCOMPARE(verdict.unsupportedReasons,
             QStringList({QStringLiteral("battery-saver-enabled")}));
}

void PerformanceBenchmarkGateTests::reportsEveryExceededBudget() {
    const PerformanceBenchmarkEnvironment environment{true, false, 144.0};
    const PerformanceBenchmarkMeasurements measurements{
        100LL * 1024 * 1024, 116LL * 1024 * 1024, 16.8, 40.0, 2};

    const PerformanceBenchmarkVerdict verdict =
        PerformanceBenchmarkGate::evaluate(environment, measurements);

    QVERIFY(verdict.supported);
    QVERIFY(!verdict.passed);
    QCOMPARE(verdict.failures,
             QStringList({QStringLiteral("working-set-growth-over-15-percent"),
                          QStringLiteral("p95-over-16.7ms"),
                          QStringLiteral("too-many-animation-bursts-over-33.3ms")}));
}

QTEST_MAIN(PerformanceBenchmarkGateTests)

#include "performance_benchmark_gate_tests.moc"
