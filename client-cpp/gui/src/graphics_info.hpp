#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QQuickWindow;

class GraphicsInfo final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString graphicsApi READ graphicsApi NOTIFY changed)
    Q_PROPERTY(QString renderer READ renderer NOTIFY changed)
    Q_PROPERTY(QString vendor READ vendor NOTIFY changed)
    Q_PROPERTY(bool hardwareAcceleration READ hardwareAcceleration NOTIFY changed)
    Q_PROPERTY(bool softwareRendering READ softwareRendering NOTIFY changed)
    Q_PROPERTY(double refreshRate READ refreshRate NOTIFY changed)
    Q_PROPERTY(double dpi READ dpi NOTIFY changed)
    Q_PROPERTY(QString resolution READ resolution NOTIFY changed)

public:
    explicit GraphicsInfo(QObject* parent = nullptr);

    QString graphicsApi() const { return graphicsApi_; }
    QString renderer() const { return renderer_; }
    QString vendor() const { return vendor_; }
    bool hardwareAcceleration() const { return hardwareAcceleration_; }
    bool softwareRendering() const { return softwareRendering_; }
    double refreshRate() const { return refreshRate_; }
    double dpi() const { return dpi_; }
    QString resolution() const { return resolution_; }

    Q_INVOKABLE void refresh();
    void attachWindow(QQuickWindow* window);
    void detachWindow();

signals:
    void changed();

private:
    QPointer<QQuickWindow> window_;
    QString graphicsApi_ = QStringLiteral("Unknown");
    QString renderer_ = QStringLiteral("Unknown");
    QString vendor_ = QStringLiteral("Unknown");
    bool hardwareAcceleration_ = false;
    bool softwareRendering_ = false;
    double refreshRate_ = 0.0;
    double dpi_ = 0.0;
    QString resolution_ = QStringLiteral("Unknown");
};
