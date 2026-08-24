#include "lan_discovery_service.hpp"
#include "openssl_runtime.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUdpSocket>

#include <openssl/pem.h>
#include <openssl/x509.h>

#include <algorithm>
#include <utility>

namespace {
constexpr quint16 kDiscoveryPort = 38888;
constexpr int kDiscoveryVersion = 1;
constexpr int kHostLifetimeMs = 4500;
constexpr int kScanDurationMs = 2500;
constexpr int kMaximumPacketSize = 16 * 1024;

void setError(QString* error, const QString& message) {
    if (error) *error = message;
}

QString fingerprintFor(const QByteArray& certificatePem) {
    return QString::fromLatin1(
        QCryptographicHash::hash(certificatePem, QCryptographicHash::Sha256).toHex());
}

bool isValidPemCertificate(const QByteArray& certificatePem) {
    BIO* bio = BIO_new_mem_buf(certificatePem.constData(), certificatePem.size());
    if (!bio) {
        return false;
    }
    X509* certificate = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    const bool valid = certificate != nullptr;
    X509_free(certificate);
    BIO_free(bio);
    return valid;
}
}  // namespace

LanDiscoveryService::LanDiscoveryService(QObject* parent, QString storageRoot)
    : QObject(parent), storageRoot_(std::move(storageRoot)) {
    expiryTimer_.setInterval(1000);
    connect(&expiryTimer_, &QTimer::timeout, this, &LanDiscoveryService::expireStaleHosts);
    scanTimer_.setSingleShot(true);
    connect(&scanTimer_, &QTimer::timeout, this, &LanDiscoveryService::finishScan);
}

bool LanDiscoveryService::refresh(QString* error) {
    if (!OpenSslRuntime::prepare(error)) {
        return false;
    }
    if (!expiryTimer_.isActive()) {
        expiryTimer_.start();
    }
    if (!socket_) {
        socket_ = new QUdpSocket(this);
        connect(socket_, &QUdpSocket::readyRead, this, &LanDiscoveryService::readPendingDatagrams);
        if (!socket_->bind(QHostAddress::AnyIPv4, kDiscoveryPort,
                           QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            setError(error, QStringLiteral("Unable to listen for LAN discovery: ") + socket_->errorString());
            socket_->deleteLater();
            socket_ = nullptr;
            return false;
        }
    }
    if (!hosts_.isEmpty()) {
        hosts_.clear();
        emit hostsChanged();
    }
    setScanning(true);
    scanTimer_.start(kScanDurationMs);
    return true;
}

QJsonArray LanDiscoveryService::hostsJson() const {
    QList<LanDiscoveredHost> hosts = hosts_.values();
    std::sort(hosts.begin(), hosts.end(), [](const LanDiscoveredHost& left, const LanDiscoveredHost& right) {
        return QString::compare(left.hostName, right.hostName, Qt::CaseInsensitive) < 0;
    });
    QJsonArray result;
    for (const LanDiscoveredHost& host : hosts) {
        result.append(QJsonObject{{"id", host.id},
                                  {"hostName", host.hostName},
                                  {"serverIp", host.serverIp},
                                  {"serverPort", host.serverPort},
                                  {"fingerprintSha256", host.fingerprintSha256},
                                  {"known", QFileInfo::exists(certificatePath(host))}});
    }
    return result;
}

bool LanDiscoveryService::connectData(const QString& hostId, LanDiscoveredHost* host, QString* error) const {
    const auto it = hosts_.constFind(hostId.trimmed().toLower());
    if (it == hosts_.cend() || it->expiresAt <= QDateTime::currentDateTimeUtc()) {
        setError(error, QStringLiteral("This LAN chat room is no longer available. Please scan again."));
        return false;
    }
    if (host) *host = *it;
    return true;
}

QString LanDiscoveryService::persistCertificate(const LanDiscoveredHost& host, QString* error) const {
    if (host.id.isEmpty() || host.certificatePem.isEmpty()) {
        setError(error, QStringLiteral("The discovery announcement does not include a host certificate."));
        return {};
    }
    const QString path = certificatePath(host);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        setError(error, QStringLiteral("Unable to create the host certificate directory."));
        return {};
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(host.certificatePem) != host.certificatePem.size() ||
        !file.commit()) {
        setError(error, QStringLiteral("Unable to save the host public certificate."));
        return {};
    }
    return path;
}

bool LanDiscoveryService::decodeAnnouncement(const QByteArray& payload, const QHostAddress& sender,
                                             const QDateTime& receivedAt, LanDiscoveredHost* host,
                                             QString* error) {
    if (!host || payload.isEmpty() || payload.size() > kMaximumPacketSize ||
        sender.protocol() != QAbstractSocket::IPv4Protocol) {
        setError(error, QStringLiteral("Invalid LAN discovery payload."));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    const QJsonObject object = document.object();
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        object.value("service").toString() != QStringLiteral("lan-chat") ||
        object.value("version").toInt() != kDiscoveryVersion) {
        setError(error, QStringLiteral("This is not a LAN Chat room announcement."));
        return false;
    }
    const QString hostName = object.value("hostName").toString().trimmed();
    const int port = object.value("port").toInt();
    const QString advertisedFingerprint = object.value("fingerprintSha256").toString().trimmed().toLower();
    const QByteArray certificatePem =
        QByteArray::fromBase64(object.value("certificateBase64").toString().toLatin1());
    if (hostName.isEmpty() || port < 1 || port > 65535 || certificatePem.isEmpty() ||
        certificatePem.size() > kMaximumPacketSize ||
        !certificatePem.startsWith("-----BEGIN CERTIFICATE-----") ||
        !isValidPemCertificate(certificatePem)) {
        setError(error, QStringLiteral("The room announcement contains an invalid certificate."));
        return false;
    }
    const QString fingerprint = fingerprintFor(certificatePem);
    if (advertisedFingerprint.size() != 64 || advertisedFingerprint != fingerprint) {
        setError(error, QStringLiteral("The room certificate fingerprint does not match the announcement."));
        return false;
    }
    *host = LanDiscoveredHost{fingerprint, hostName, sender.toString(), port, fingerprint,
                              certificatePem, receivedAt.toUTC().addMSecs(kHostLifetimeMs)};
    return true;
}

void LanDiscoveryService::readPendingDatagrams() {
    bool changed = false;
    while (socket_ && socket_->hasPendingDatagrams()) {
        QByteArray payload;
        payload.resize(static_cast<int>(socket_->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        socket_->readDatagram(payload.data(), payload.size(), &sender, &senderPort);
        Q_UNUSED(senderPort);
        LanDiscoveredHost host;
        if (!decodeAnnouncement(payload, sender, QDateTime::currentDateTimeUtc(), &host)) {
            continue;
        }
        const auto existing = hosts_.constFind(host.id);
        if (existing == hosts_.cend() || existing->serverIp != host.serverIp ||
            existing->expiresAt.secsTo(host.expiresAt) != 0) {
            changed = true;
        }
        hosts_.insert(host.id, host);
    }
    if (changed) emit hostsChanged();
}

void LanDiscoveryService::expireStaleHosts() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    bool changed = false;
    for (auto it = hosts_.begin(); it != hosts_.end();) {
        if (it->expiresAt <= now) {
            it = hosts_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) emit hostsChanged();
}

void LanDiscoveryService::finishScan() {
    setScanning(false);
}

QString LanDiscoveryService::storageRoot() const {
    if (!storageRoot_.isEmpty()) return storageRoot_;
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

QString LanDiscoveryService::certificatePath(const LanDiscoveredHost& host) const {
    return QDir(storageRoot()).filePath(QStringLiteral("known-hosts/%1.crt").arg(host.id));
}

void LanDiscoveryService::setScanning(bool scanning) {
    if (scanning_ == scanning) return;
    scanning_ = scanning;
    emit scanningChanged();
}
