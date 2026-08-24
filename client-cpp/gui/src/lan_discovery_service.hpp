#pragma once

#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QTimer>

class QUdpSocket;

struct LanDiscoveredHost {
    QString id;
    QString hostName;
    QString serverIp;
    int serverPort = 0;
    QString fingerprintSha256;
    QByteArray certificatePem;
    QDateTime expiresAt;
};

// Receives LAN Chat's public UDP advertisements. It never accepts a private
// key and persists only the selected host's public certificate.
class LanDiscoveryService final : public QObject {
    Q_OBJECT

public:
    explicit LanDiscoveryService(QObject* parent = nullptr, QString storageRoot = {});

    bool refresh(QString* error = nullptr);
    QJsonArray hostsJson() const;
    bool scanning() const { return scanning_; }
    bool connectData(const QString& hostId, LanDiscoveredHost* host, QString* error) const;
    QString persistCertificate(const LanDiscoveredHost& host, QString* error) const;

    static bool decodeAnnouncement(const QByteArray& payload, const QHostAddress& sender,
                                   const QDateTime& receivedAt, LanDiscoveredHost* host,
                                   QString* error = nullptr);

signals:
    void hostsChanged();
    void scanningChanged();

private slots:
    void readPendingDatagrams();
    void expireStaleHosts();
    void finishScan();

private:
    QString storageRoot() const;
    QString certificatePath(const LanDiscoveredHost& host) const;
    void setScanning(bool scanning);

    QUdpSocket* socket_ = nullptr;
    QTimer expiryTimer_;
    QTimer scanTimer_;
    QHash<QString, LanDiscoveredHost> hosts_;
    QString storageRoot_;
    bool scanning_ = false;
};
