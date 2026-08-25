#pragma once

#include <QStringList>

struct PerformanceBenchmarkEnvironment {
    bool hardwareAcceleration = false;
    bool softwareRendering = false;
    double refreshRateHz = 0.0;
    bool batterySaverEnabled = false;
};

struct PerformanceBenchmarkMeasurements {
    qint64 warmUpWorkingSetBytes = 0;
    qint64 peakWorkingSetBytes = 0;
    double p95FrameMs = 0.0;
    double p99FrameMs = 0.0;
    int animationBurstsOver33Ms = 0;
};

struct PerformanceBenchmarkVerdict {
    bool supported = false;
    bool passed = false;
    double workingSetGrowthPercent = 0.0;
    QStringList unsupportedReasons;
    QStringList failures;
};

class PerformanceBenchmarkGate final {
public:
    static PerformanceBenchmarkVerdict evaluate(const PerformanceBenchmarkEnvironment& environment,
                                                const PerformanceBenchmarkMeasurements& measurements);
};
