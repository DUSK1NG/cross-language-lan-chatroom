#include "performance_benchmark_gate.hpp"

namespace {
constexpr double kMinimumRefreshRateHz = 60.0;
constexpr double kMaximumWorkingSetGrowthPercent = 15.0;
constexpr double kMaximumP95FrameMs = 16.7;
constexpr int kMaximumAnimationBurstsOver33Ms = 1;
}

PerformanceBenchmarkVerdict PerformanceBenchmarkGate::evaluate(
    const PerformanceBenchmarkEnvironment& environment,
    const PerformanceBenchmarkMeasurements& measurements) {
    PerformanceBenchmarkVerdict verdict;

    if (environment.softwareRendering) {
        verdict.unsupportedReasons.append(QStringLiteral("software-rendering"));
    }
    if (!environment.hardwareAcceleration) {
        verdict.unsupportedReasons.append(QStringLiteral("no-hardware-acceleration"));
    }
    if (environment.refreshRateHz < kMinimumRefreshRateHz) {
        verdict.unsupportedReasons.append(QStringLiteral("refresh-rate-below-60hz"));
    }
    if (environment.batterySaverEnabled) {
        verdict.unsupportedReasons.append(QStringLiteral("battery-saver-enabled"));
    }

    verdict.supported = verdict.unsupportedReasons.isEmpty();
    if (!verdict.supported) {
        return verdict;
    }

    if (measurements.warmUpWorkingSetBytes <= 0 ||
        measurements.peakWorkingSetBytes < measurements.warmUpWorkingSetBytes) {
        verdict.failures.append(QStringLiteral("invalid-working-set-sample"));
    } else {
        verdict.workingSetGrowthPercent =
            100.0 * static_cast<double>(measurements.peakWorkingSetBytes -
                                        measurements.warmUpWorkingSetBytes) /
            static_cast<double>(measurements.warmUpWorkingSetBytes);
        if (verdict.workingSetGrowthPercent > kMaximumWorkingSetGrowthPercent) {
            verdict.failures.append(QStringLiteral("working-set-growth-over-15-percent"));
        }
    }

    if (measurements.p95FrameMs > kMaximumP95FrameMs) {
        verdict.failures.append(QStringLiteral("p95-over-16.7ms"));
    }
    if (measurements.animationBurstsOver33Ms > kMaximumAnimationBurstsOver33Ms) {
        verdict.failures.append(QStringLiteral("too-many-animation-bursts-over-33.3ms"));
    }

    verdict.passed = verdict.failures.isEmpty();
    return verdict;
}
