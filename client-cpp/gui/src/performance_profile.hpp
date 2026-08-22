#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QPointer>
#include <QVector>

class QQuickWindow;

class PerformanceProfile final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QString effectiveMode READ effectiveMode NOTIFY effectiveModeChanged)
    Q_PROPERTY(bool effectsEnabled READ effectsEnabled NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool gradientsEnabled READ gradientsEnabled NOTIFY capabilitiesChanged)
    Q_PROPERTY(double animationDurationScale READ animationDurationScale NOTIFY capabilitiesChanged)
    Q_PROPERTY(int observedFrameCount READ observedFrameCount NOTIFY observedFrameCountChanged)
    Q_PROPERTY(double observedFps READ observedFps NOTIFY metricsChanged)
    Q_PROPERTY(double observedP95FrameMs READ observedP95FrameMs NOTIFY metricsChanged)
    Q_PROPERTY(double observedMaxFrameMs READ observedMaxFrameMs NOTIFY metricsChanged)
    Q_PROPERTY(QString automaticReason READ automaticReason NOTIFY automaticReasonChanged)

public:
    explicit PerformanceProfile(QObject* parent = nullptr);

    QString mode() const { return mode_; }
    QString effectiveMode() const { return effectiveMode_; }
    bool effectsEnabled() const { return effectsEnabled_; }
    bool animationsEnabled() const { return animationsEnabled_; }
    bool gradientsEnabled() const { return gradientsEnabled_; }
    double animationDurationScale() const { return animationDurationScale_; }
    int observedFrameCount() const { return frameTimesMs_.size(); }
    double observedFps() const { return observedFps_; }
    double observedP95FrameMs() const { return observedP95FrameMs_; }
    double observedMaxFrameMs() const { return observedMaxFrameMs_; }
    QString automaticReason() const { return automaticReason_; }

    void setMode(const QString& mode);
    void attachWindow(QQuickWindow* window);
    void detachWindow();
    Q_INVOKABLE void updateGraphicsContext(bool hardwareAcceleration,
                                           bool softwareRendering,
                                           double refreshRate);
    Q_INVOKABLE void observeFrameTime(double frameMs);

signals:
    void modeChanged();
    void effectiveModeChanged();
    void capabilitiesChanged();
    void observedFrameCountChanged();
    void metricsChanged();
    void automaticReasonChanged();

private:
    void handleFrameSwapped();
    void recomputeMetrics();
    void recompute();
    QString automaticMode() const;
    QString automaticReasonForContext() const;
    static QString normalizeMode(const QString& mode);

    QString mode_ = QStringLiteral("Automatic");
    QString effectiveMode_ = QStringLiteral("Balanced");
    bool effectsEnabled_ = true;
    bool animationsEnabled_ = true;
    bool gradientsEnabled_ = true;
    double animationDurationScale_ = 0.75;
    bool hardwareAcceleration_ = false;
    bool softwareRendering_ = false;
    double refreshRate_ = 60.0;
    QVector<double> frameTimesMs_;
    double observedFps_ = 0.0;
    double observedP95FrameMs_ = 0.0;
    double observedMaxFrameMs_ = 0.0;
    QString automaticReason_ = QStringLiteral("waiting-for-samples");
    QString pendingAutomaticMode_;
    int pendingAutomaticModeStreak_ = 0;
    QPointer<QQuickWindow> window_;
    QElapsedTimer frameTimer_;
    qint64 lastFrameTimestampNs_ = 0;
    int framesSinceSample_ = 0;
};
