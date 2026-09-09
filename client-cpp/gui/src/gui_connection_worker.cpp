#include "gui_connection_worker.hpp"
#include "attachments/attachment_mls_group_id.hpp"
#include "network_diagnostics.hpp"
#include "openssl_runtime.hpp"

#include <utility>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QVariantMap>
#include "json.hpp"
namespace {
constexpr int kLocalHostPort = 8888;
constexpr int kLocalHostProbeTimeoutMs = 150;
constexpr int kLocalHostStartupTimeoutMs = 4000;
constexpr int kLocalHostRetryAttempts = 8;
constexpr int kLocalHostRetryDelayMs = 150;
constexpr int kWelcomeAcceptCommandDigestSize = 56;

QString welcomeAcceptCommandId(const QString& groupId, std::uint64_t epoch) {
    const QByteArray material = groupId.toUtf8() + '|' + QByteArray::number(epoch);
    const QByteArray digest = QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex();
    // The Go transport caps command_id at 64 bytes. Keep this deterministic so retries
    // of the same welcome are recognized as the same command.
    return QStringLiteral("wa-") + QString::fromLatin1(digest.left(kWelcomeAcceptCommandDigestSize));
}

QString userFacingLoginFailure(const QString& reason) {
    if (reason == QStringLiteral("The room owner declined this connection")) {
        return QStringLiteral("房主拒绝了此次连接请求。");
    }
    if (reason == QStringLiteral("The room owner did not approve the connection in time")) {
        return QStringLiteral("等待房主确认连接超时，请稍后重试。");
    }
    return QStringLiteral("登录被拒绝：") + reason;
}

#ifdef LAN_CHAT_ENABLE_MLSPP
bool attachmentKeyFromBytes(const QByteArray& bytes, attachments::AttachmentCrypto::Key& key) {
    if (bytes.size() != static_cast<int>(attachments::AttachmentCrypto::KeySize)) return false;
    std::copy(bytes.cbegin(), bytes.cend(), key.begin());
    return true;
}
#endif

#ifdef LAN_CHAT_ENABLE_MLSPP
constexpr int kMaxMlsOpaqueBytes = 48 * 1024;

QByteArray encodeMlsOpaque(const MLS_NAMESPACE::bytes_ns::bytes& value)
{
    if (value.empty()) return {};
    const auto raw = QByteArray(reinterpret_cast<const char*>(value.data()),
                                static_cast<int>(value.size()));
    const auto encoded = raw.toBase64();
    return encoded.size() <= kMaxMlsOpaqueBytes ? encoded : QByteArray();
}

MLS_NAMESPACE::bytes_ns::bytes decodeMlsOpaque(const std::string& value)
{
    const auto encoded = QByteArray::fromStdString(value);
    if (encoded.isEmpty() || encoded.size() > kMaxMlsOpaqueBytes ||
        QByteArray::fromBase64(encoded).toBase64() != encoded) {
        throw std::invalid_argument("invalid MLS opaque value");
    }
    const auto raw = QByteArray::fromBase64(encoded);
    MLS_NAMESPACE::bytes_ns::bytes result(raw.size());
    std::copy_n(reinterpret_cast<const std::uint8_t*>(raw.constData()), raw.size(), result.begin());
    return result;
}

MLS_NAMESPACE::bytes_ns::bytes mlsIdentity(const QString& userCode)
{
    const auto identity = userCode.toUtf8();
    MLS_NAMESPACE::bytes_ns::bytes result(identity.size());
    std::copy_n(reinterpret_cast<const std::uint8_t*>(identity.constData()), identity.size(), result.begin());
    return result;
}

MLS_NAMESPACE::bytes_ns::bytes mlsBytes(const QByteArray& value)
{
    MLS_NAMESPACE::bytes_ns::bytes result(value.size());
    std::copy_n(reinterpret_cast<const std::uint8_t*>(value.constData()), value.size(), result.begin());
    return result;
}
#endif

}

GuiConnectionWorker::GuiConnectionWorker(QObject* parent) : QObject(parent) {}

GuiConnectionWorker::~GuiConnectionWorker() {
    stopReceiveLoop();
    stopHostedServer();
}

void GuiConnectionWorker::connectToServer(const QString& serverIp,
                                          int serverPort,
                                          const QString& username,
                                          const QString& userCode,
                                          const QString& caFile,
                                          const QString& tlsServerName) {
    stopReceiveLoop();
    explicitDisconnect_ = false;
    reconnectPolicy_.markConnected();
    reconnectTimerActive_ = false;
    const bool preserveMlsState = savedConnection_.valid &&
                                  savedConnection_.userCode.compare(userCode, Qt::CaseInsensitive) == 0;
    savedConnection_ = {serverIp, serverPort, username, userCode, caFile, tlsServerName, true};
    NetworkDiagnostics::writeConnectionEvent(QStringLiteral("connect_requested"), serverIp, serverPort,
                                              0, tlsServerName.isEmpty() ? QString() : QStringLiteral("tls_name=") + tlsServerName);
    connectToServerWithRetries(serverIp, serverPort, username, userCode, caFile, tlsServerName, 1,
                               true, preserveMlsState);
}

bool GuiConnectionWorker::connectToServerWithRetries(const QString& serverIp,
                                                      int serverPort,
                                                      const QString& username,
                                                      const QString& userCode,
                                                      const QString& caFile,
                                                      const QString& tlsServerName,
                                                      const int attempts,
                                                      const bool reportFailure,
                                                      const bool preserveMlsState) {
    const int boundedAttempts = qMax(1, attempts);
    QString lastReason;
    connection::LoginResult loginResult = connection::LoginResult::kRetryableFailure;

    if (!OpenSslRuntime::prepare(&lastReason)) {
        lastConnectionFailure_ = lastReason;
        if (reportFailure) emit connectionFailed(lastReason);
        return false;
    }

    for (int attempt = 0; attempt < boundedAttempts; ++attempt) {
        NetworkDiagnostics::writeConnectionEvent(QStringLiteral("connect_attempt"), serverIp, serverPort, attempt + 1);
        connection::Config config{
            serverIp.toStdString(),
            serverPort,
            username.toStdString(),
            userCode.toStdString(),
            caFile.toStdString(),
            tlsServerName.toStdString(),
        };
        connection_ = std::make_unique<connection::ConnectionState>(std::move(config));

        message::Message loginResponse;
        if (connection_->connect_and_login(loginResponse, loginResult)) {
            NetworkDiagnostics::writeConnectionEvent(QStringLiteral("login_ok"), serverIp, serverPort, attempt + 1);
            running_.store(true);
            receiveThread_ = std::thread(&GuiConnectionWorker::receiveLoop, this);
#ifdef LAN_CHAT_ENABLE_MLSPP
            if (!preserveMlsState) {
                resetMlsState();
            } else if (!mlsClient_) {
                try {
                    mlsClient_ = std::make_shared<MlsClient>(MlsClient::create(mlsIdentity(savedConnection_.userCode)));
                } catch (...) {
                    mlsClient_.reset();
                }
            }
            publishMlsKeyPackage();
            if (preserveMlsState) {
                resumePendingMlsOperations();
            }
#endif
            emit connected(loginResponse.is_admin);
            return true;
        }

        lastReason = QString::fromStdString(connection_->last_error());
        NetworkDiagnostics::writeConnectionEvent(QStringLiteral("connect_failed"), serverIp, serverPort,
                                                  attempt + 1, lastReason);
        lastConnectionFailure_ = lastReason;
        connection_.reset();
        if (loginResult == connection::LoginResult::kRejected) {
            lastConnectionFailure_ = userFacingLoginFailure(lastReason);
            if (reportFailure) emit connectionFailed(lastConnectionFailure_);
            return false;
        }
        if (attempt + 1 < boundedAttempts) {
            QThread::msleep(kLocalHostRetryDelayMs);
        }
    }

    lastConnectionFailure_ = lastReason.isEmpty() ? QStringLiteral("Connection failed") : lastReason;
    if (reportFailure) emit connectionFailed(lastConnectionFailure_);
    return false;
}

void GuiConnectionWorker::connectToLocalHost(const QString& serverExe,
                                              const QString& certFile,
                                              const QString& keyFile,
                                              const QString& dbFile,
                                              const QString& username,
                                              const QString& userCode) {
    const auto resolvePath = [](const QString& path) {
        const QFileInfo info(path);
        if (info.isAbsolute()) return info.absoluteFilePath();
        return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(path);
    };
    const QString absoluteServerExe = resolvePath(serverExe);
    const QString absoluteCertFile = resolvePath(certFile);
    const QString absoluteKeyFile = resolvePath(keyFile);
    const QString absoluteDbFile = resolvePath(dbFile);

    stopReceiveLoop();
    connection_.reset();
    explicitDisconnect_ = false;
    reconnectPolicy_.markConnected();
    reconnectTimerActive_ = false;
    savedConnection_ = {QStringLiteral("127.0.0.1"), kLocalHostPort, username, userCode,
                        absoluteCertFile, QString(), true};

    if (!QFileInfo::exists(absoluteServerExe)) {
        emit connectionFailed(QStringLiteral("本地 Go Server 文件不存在，请检查 Host 路径"));
        return;
    }

    const bool localIdentityReady = QFileInfo::exists(absoluteCertFile) && QFileInfo::exists(absoluteKeyFile);
    if (isLocalServerListening(kLocalHostProbeTimeoutMs) && localIdentityReady) {
        connectToServerWithRetries(QStringLiteral("127.0.0.1"), kLocalHostPort,
                                   username, userCode, absoluteCertFile, QString(), 2);
        return;
    }
    if (isLocalServerListening(kLocalHostProbeTimeoutMs)) {
        emit connectionFailed(QStringLiteral("Local port 8888 is already in use before the local TLS identity was initialized."));
        return;
    }

    stopHostedServer();
    hostProcess_ = std::make_unique<QProcess>();
    hostProcess_->setProcessChannelMode(QProcess::SeparateChannels);
    if (NetworkDiagnostics::enabled()) {
        const QString hostLogPath = NetworkDiagnostics::hostServerLogFilePath();
        hostProcess_->setStandardOutputFile(hostLogPath, QIODevice::Append);
        hostProcess_->setStandardErrorFile(hostLogPath, QIODevice::Append);
        NetworkDiagnostics::writeConnectionEvent(QStringLiteral("host_process_start"), QStringLiteral("127.0.0.1"),
                                                  kLocalHostPort);
    }
    hostProcess_->setProgram(absoluteServerExe);
    hostProcess_->setWorkingDirectory(QFileInfo(absoluteServerExe).absolutePath());
    hostProcess_->setArguments({"-cert", absoluteCertFile, "-key", absoluteKeyFile, "-auto-cert", "-db", absoluteDbFile,
                                "-admin-code", userCode, "-lan-discovery", "-discovery-name", username});
    hostProcess_->start();
    if (!hostProcess_->waitForStarted(3000)) {
        emit connectionFailed(QStringLiteral("无法启动本地 Go Server：") + hostProcess_->errorString());
        hostProcess_.reset();
        return;
    }
    QString startupLog;
    QElapsedTimer startupTimer;
    startupTimer.start();
    bool listening = false;
    while (startupTimer.elapsed() < kLocalHostStartupTimeoutMs) {
        const int remaining = kLocalHostStartupTimeoutMs - static_cast<int>(startupTimer.elapsed());
        hostProcess_->waitForReadyRead(qMin(200, qMax(1, remaining)));
        startupLog += QString::fromLocal8Bit(hostProcess_->readAllStandardError());
        startupLog += QString::fromLocal8Bit(hostProcess_->readAllStandardOutput());
        if (hostProcess_->state() == QProcess::NotRunning) break;
        if (isLocalServerListening(kLocalHostProbeTimeoutMs)) {
            listening = true;
            break;
        }
    }
    if (!listening) {
        startupLog += QString::fromLocal8Bit(hostProcess_->readAllStandardError());
        startupLog += QString::fromLocal8Bit(hostProcess_->readAllStandardOutput());
        const QString detail = startupLog.trimmed();
        emit connectionFailed(QStringLiteral("Local Go Server did not become ready") +
                              (detail.isEmpty() ? QString() : QStringLiteral(": ") + detail));
        stopHostedServer();
        return;
    }
    if (hostProcess_->state() != QProcess::Running) {
        const QString error = QString::fromLocal8Bit(hostProcess_->readAllStandardError()).trimmed();
        emit connectionFailed(QStringLiteral("本地 Go Server 启动后退出") +
                              (error.isEmpty() ? QString() : QStringLiteral("：") + error));
        hostProcess_.reset();
        return;
    }
    if (!QFileInfo::exists(absoluteCertFile) || !QFileInfo::exists(absoluteKeyFile)) {
        emit connectionFailed(QStringLiteral("本地 Go Server 未生成 TLS 证书，请检查 Host 路径和目录写入权限。"));
        stopHostedServer();
        return;
    }
    connectToServerWithRetries(QStringLiteral("127.0.0.1"), kLocalHostPort,
                               username, userCode, absoluteCertFile, QString(), kLocalHostRetryAttempts);
}

void GuiConnectionWorker::disconnectFromServer() {
    explicitDisconnect_ = true;
    reconnectPolicy_.cancel();
    reconnectTimerActive_ = false;
    stopReceiveLoop();
    connection_.reset();
    stopHostedServer();
    emit disconnected();
}

void GuiConnectionWorker::scheduleReconnect() {
    if (explicitDisconnect_ || !savedConnection_.valid || reconnectTimerActive_) return;
    stopReceiveLoop();
    connection_.reset();
    const int delayMs = reconnectPolicy_.scheduleNextAttempt();
    if (delayMs < 0) return;
    NetworkDiagnostics::writeConnectionEvent(QStringLiteral("reconnect_scheduled"), savedConnection_.serverIp,
                                              savedConnection_.serverPort, reconnectPolicy_.attemptCount(),
                                              QStringLiteral("delay_ms=") + QString::number(delayMs));
    emit reconnectScheduled(reconnectPolicy_.attemptCount(), delayMs);
    reconnectTimerActive_ = true;
    const int reconnectGeneration = reconnectPolicy_.generation();
    QTimer::singleShot(delayMs, this, [this, reconnectGeneration]() {
        if (reconnectGeneration != reconnectPolicy_.generation()) return;
        reconnectTimerActive_ = false;
        retrySavedConnection();
    });
}

void GuiConnectionWorker::retrySavedConnection() {
    if (explicitDisconnect_ || !savedConnection_.valid) return;
    NetworkDiagnostics::writeConnectionEvent(QStringLiteral("reconnect_attempt"), savedConnection_.serverIp,
                                              savedConnection_.serverPort, reconnectPolicy_.attemptCount());
    emit reconnectAttempt(reconnectPolicy_.attemptCount());
    if (connectToServerWithRetries(savedConnection_.serverIp, savedConnection_.serverPort,
                                   savedConnection_.username, savedConnection_.userCode,
                                   savedConnection_.caFile, savedConnection_.tlsServerName, 1, false, true)) {
        reconnectPolicy_.markConnected();
        return;
    }
    emit reconnectFailed(lastConnectionFailure_);
    scheduleReconnect();
}

bool GuiConnectionWorker::isLocalServerListening(const int timeoutMs) const {
    QTcpSocket probe;
    probe.connectToHost(QStringLiteral("127.0.0.1"), kLocalHostPort);
    const bool connected = probe.waitForConnected(qMax(1, timeoutMs));
    probe.abort();
    return connected;
}

void GuiConnectionWorker::stopHostedServer() {
    if (!hostProcess_) return;
    NetworkDiagnostics::writeConnectionEvent(QStringLiteral("host_process_stop"), QStringLiteral("127.0.0.1"),
                                              kLocalHostPort);
    if (hostProcess_->state() != QProcess::NotRunning) {
        hostProcess_->terminate();
        if (!hostProcess_->waitForFinished(1500)) {
            hostProcess_->kill();
            hostProcess_->waitForFinished(500);
        }
    }
    hostProcess_.reset();
}

void GuiConnectionWorker::sendChatToRoom(const QString& content, const QString& room, const QString& messageId) {
    if (!connection_ || !connection_->is_ready() || content.trimmed().isEmpty()) {
        if (!messageId.isEmpty()) emit messageDeliveryFailed(messageId);
        return;
    }
    message::Message message{
        "chat", "", "", content.trimmed().toStdString(), {}, "", room.toStdString(), {}, ""};
    message.message_id = messageId.toStdString();
    if (!connection_->send(message)) {
        if (!messageId.isEmpty()) emit messageDeliveryFailed(messageId);
        emit connectionLost(QString::fromStdString(connection_->last_error()));
    }
}

void GuiConnectionWorker::sendPrivate(const QString& content, const QString& targetUserCode, const QString& messageId) {
    if (!connection_ || !connection_->is_ready() || content.trimmed().isEmpty() || targetUserCode.isEmpty()) {
        if (!messageId.isEmpty()) emit messageDeliveryFailed(messageId);
        return;
    }
    message::Message message{
        "private_chat", "", "", content.trimmed().toStdString(), {},
        targetUserCode.toStdString(), "", {}, ""};
    message.message_id = messageId.toStdString();
    if (!connection_->send(message)) {
        if (!messageId.isEmpty()) emit messageDeliveryFailed(messageId);
        emit connectionLost(QString::fromStdString(connection_->last_error()));
    }
}

void GuiConnectionWorker::joinRoom(const QString& room) {
    if (!connection_ || !connection_->is_ready() || room.trimmed().isEmpty()) {
        return;
    }
    const message::Message message{"room_join", "", "", "", {}, "",
                                  room.trimmed().toStdString(), {}, ""};
    if (!connection_->send(message)) {
        emit connectionLost(QString::fromStdString(connection_->last_error()));
    }
}

void GuiConnectionWorker::createRoom(const QString& room, bool isPrivate) {
    if (!connection_ || !connection_->is_ready() || room.trimmed().isEmpty()) {
        return;
    }
    message::Message message{"room_create", "", "", "", {}, "",
                             room.trimmed().toStdString(), {}, ""};
    message.is_private = isPrivate;
    if (!connection_->send(message)) {
        emit connectionLost(QString::fromStdString(connection_->last_error()));
    }
}

void GuiConnectionWorker::sendRoomAction(const QString& action, const QString& room, const QString& targetUserCode) {
    if (!connection_ || !connection_->is_ready() || action.trimmed().isEmpty() || room.trimmed().isEmpty()) {
        return;
    }
    const message::Message message{"room_action", "", "", action.trimmed().toStdString(), {},
                                   targetUserCode.trimmed().toStdString(), room.trimmed().toStdString(), {}, ""};
    if (!connection_->send(message)) {
        emit connectionLost(QString::fromStdString(connection_->last_error()));
    }
}

void GuiConnectionWorker::requestUsers() {
    if (!connection_ || !connection_->is_ready()) {
        return;
    }
    const message::Message message{"users_request", "", "", "", {}, "", "", {}, ""};
    if (!connection_->send(message)) {
        emit connectionLost(QString::fromStdString(connection_->last_error()));
    }
}

void GuiConnectionWorker::requestRooms() {
    if (!connection_ || !connection_->is_ready()) return;
    const message::Message message{"rooms_request", "", "", "", {}, "", "", {}, ""};
    if (!connection_->send(message)) {
        emit connectionLost(QString::fromStdString(connection_->last_error()));
    }
}

void GuiConnectionWorker::requestHistory(const QString& room, const QString& targetUserCode,
                                         bool isPrivate, const QString& beforeMessageId, int limit,
                                         const QString& searchQuery) {
    if (!connection_ || !connection_->is_ready()) return;
    message::Message message{"history_request", "", "", "", {},
                             targetUserCode.trimmed().toStdString(), room.trimmed().toStdString(), {}, ""};
    message.is_private = isPrivate;
    message.before_message_id = beforeMessageId.trimmed().toStdString();
    message.search_query = searchQuery.trimmed().toStdString();
    message.limit = qBound(1, limit, 100);
    if (!connection_->send(message)) {
        emit connectionLost(QString::fromStdString(connection_->last_error()));
    }
}

void GuiConnectionWorker::sendAdminAction(const QString& action, const QString& targetUserCode, const QString& messageId, const QString& commandId) {
    if (!connection_ || !connection_->is_ready() || action.trimmed().isEmpty() ||
        (action.trimmed() != QStringLiteral("recall") && action.trimmed() != QStringLiteral("approve_connection") &&
         action.trimmed() != QStringLiteral("deny_connection") && targetUserCode.trimmed().isEmpty())) {
        return;
    }
    const message::Message message{
        "admin_action", "", "", action.trimmed().toStdString(), {},
        targetUserCode.trimmed().toStdString(), "", {}, "", messageId.trimmed().toStdString(), commandId.trimmed().toStdString()};
    if (!connection_->send(message)) {
        emit connectionLost(QString::fromStdString(connection_->last_error()));
    }
}

void GuiConnectionWorker::sendAttachmentInit(const QString& room, qint64 logicalSize, const QString& commandId) {
    if (logicalSize > attachments::MaxLogicalBytes) {
        emit attachmentEvent(QStringLiteral("error"), {}, {}, commandId, 0, 0, {}, {}, {}, {},
                             QStringLiteral("文件超过 5 GiB 上限"));
        return;
    }
    const QString normalizedRoom = room.trimmed();
    NetworkDiagnostics::writeConnectionEvent(QStringLiteral("attachment_init"), savedConnection_.serverIp,
                                              savedConnection_.serverPort, reconnectPolicy_.attemptCount(),
                                              QStringLiteral("room=") + normalizedRoom);
    if (!connection_ || !connection_->is_ready() || normalizedRoom.isEmpty() || logicalSize <= 0) {
        emit attachmentEvent(QStringLiteral("error"), {}, {}, commandId, 0, 0, {}, {}, {}, {},
                             QStringLiteral("invalid attachment initialization"));
        return;
    }
    message::Message message{"attachment.init"};
    message.room = normalizedRoom.toStdString();
    message.logical_size = logicalSize;
    message.command_id = commandId.trimmed().toStdString();
    const QString normalizedCommand = commandId.trimmed();
    attachments::AttachmentTransferState transfer;
    if (normalizedCommand.isEmpty() || !transfer.begin(logicalSize, attachments::TransferClient::ChunkSize)) {
        emit attachmentEvent(QStringLiteral("error"), {}, {}, commandId, 0, 0, {}, {}, {}, {},
                             QStringLiteral("attachment command id and transfer state are required"));
        return;
    }
    pendingAttachmentInits_[normalizedCommand] = std::move(transfer);
    if (!connection_->send(message)) {
        const QString reason = QString::fromStdString(connection_->last_error());
        pendingAttachmentInits_.erase(normalizedCommand);
        emit attachmentEvent(QStringLiteral("error"), {}, {}, commandId, 0, 0, {}, {}, {}, {}, reason);
        emit connectionLost(reason);
    }
}

void GuiConnectionWorker::startAttachmentUpload(const QString& room, const QString& filePath,
                                                const QStringList& targetUsers, const QString& commandId) {
    const QFileInfo fileInfo(filePath);
    if (fileInfo.size() > attachments::MaxLogicalBytes) {
        emit attachmentEvent(QStringLiteral("error"), {}, {}, commandId, 0, 0, {}, {}, {}, {},
                             QStringLiteral("文件超过 5 GiB 上限"));
        return;
    }
    attachments::AttachmentCrypto::Key key{};
    const QString normalizedCommand = commandId.trimmed();
#ifndef LAN_CHAT_ENABLE_MLSPP
    emit attachmentEvent(QStringLiteral("error"), {}, {}, commandId, 0, 0, {}, {}, {}, {},
                         QStringLiteral("MLS++ is required before sending an attachment"));
    return;
#else
    const QString normalizedRoom = room.trimmed();
    NetworkDiagnostics::writeConnectionEvent(QStringLiteral("attachment_upload_start"), savedConnection_.serverIp,
                                              savedConnection_.serverPort, reconnectPolicy_.attemptCount(),
                                              QStringLiteral("room=") + normalizedRoom);
    if (!fileInfo.isFile() || !fileInfo.isReadable() || normalizedCommand.isEmpty() || targetUsers.isEmpty()) {
        emit attachmentEvent(QStringLiteral("error"), {}, {}, commandId, 0, 0, {}, {}, {}, {},
                             targetUsers.isEmpty()
                                 ? QStringLiteral("没有可用的在线成员来建立端到端加密群组")
                                 : QStringLiteral("invalid attachment file or command"));
        return;
    }
    try {
        const QString groupId = attachments::attachment_mls_group_id(normalizedRoom, normalizedCommand);
        MlsGroupState state;
        state.client = std::make_shared<MlsClient>(MlsClient::create(mlsIdentity(savedConnection_.userCode)));
        state.client->createGroup(mlsBytes(groupId.toUtf8()));
        state.room = normalizedRoom;
        mlsGroups_.emplace(groupId, std::move(state));
        pendingAttachmentMlsSetups_[normalizedCommand] = {
            normalizedRoom, fileInfo.absoluteFilePath(), groupId, normalizedCommand, targetUsers, 0};
        startNextAttachmentMlsSetup(normalizedCommand);
    } catch (const std::exception& error) {
        emit attachmentEvent(QStringLiteral("error"), {}, {}, commandId, 0, 0, {}, {}, {}, {},
                             QString::fromUtf8(error.what()));
    }
    return;
#endif
    if (!fileInfo.isFile() || !fileInfo.isReadable() || normalizedCommand.isEmpty() ||
        !attachments::AttachmentCrypto::generate_key(key)) {
        emit attachmentEvent(QStringLiteral("error"), {}, {}, commandId, 0, 0, {}, {}, {}, {},
                             QStringLiteral("invalid attachment file or key generation failed"));
        return;
    }
    pendingAttachmentFiles_[normalizedCommand] =
        {room.trimmed(), {}, fileInfo.absoluteFilePath(), key, fileInfo.size()};
    sendAttachmentInit(room, fileInfo.size(), normalizedCommand);
}

#ifdef LAN_CHAT_ENABLE_MLSPP
void GuiConnectionWorker::startNextAttachmentMlsSetup(const QString& attachmentCommandId) {
    const auto setupIt = pendingAttachmentMlsSetups_.find(attachmentCommandId);
    if (setupIt == pendingAttachmentMlsSetups_.end()) return;
    auto& setup = setupIt->second;
    if (setup.nextTarget >= setup.targetUsers.size()) {
        const QFileInfo fileInfo(setup.filePath);
        attachments::AttachmentCrypto::Key key{};
        if (!fileInfo.isFile() || !fileInfo.isReadable() || !attachments::AttachmentCrypto::generate_key(key)) {
            failAttachmentMlsSetup(attachmentCommandId, QStringLiteral("invalid attachment file or key generation failed"));
            return;
        }
        pendingAttachmentFiles_[attachmentCommandId] =
            {setup.room, setup.groupId, fileInfo.absoluteFilePath(), key, fileInfo.size()};
        const QString room = setup.room;
        pendingAttachmentMlsSetups_.erase(setupIt);
        sendAttachmentInit(room, fileInfo.size(), attachmentCommandId);
        return;
    }
    const QString target = setup.targetUsers.at(setup.nextTarget);
    const QString fetchCommand = attachmentCommandId + QStringLiteral("-mls-fetch-%1").arg(setup.nextTarget);
    pendingAttachmentMlsFetches_[fetchCommand] = attachmentCommandId;
    fetchMlsKeyPackage(setup.room, target, fetchCommand);
}

void GuiConnectionWorker::failAttachmentMlsSetup(const QString& attachmentCommandId, const QString& reason) {
    pendingAttachmentMlsSetups_.erase(attachmentCommandId);
    for (auto it = pendingAttachmentMlsFetches_.begin(); it != pendingAttachmentMlsFetches_.end();) {
        if (it->second == attachmentCommandId) it = pendingAttachmentMlsFetches_.erase(it);
        else ++it;
    }
    for (auto it = pendingAttachmentMlsAdds_.begin(); it != pendingAttachmentMlsAdds_.end();) {
        if (it->second == attachmentCommandId) it = pendingAttachmentMlsAdds_.erase(it);
        else ++it;
    }
    emit attachmentEvent(QStringLiteral("error"), {}, {}, attachmentCommandId, 0, 0, {}, {}, {}, {}, reason);
}
#endif

void GuiConnectionWorker::sendAttachmentChunk(const QString& uploadId, qint64 chunkIndex,
                                               const QByteArray& ciphertext, const QByteArray& cipherSha256,
                                               const QString& commandId) {
    if (!connection_ || !connection_->is_ready() || uploadId.trimmed().isEmpty() || chunkIndex < 0 ||
        ciphertext.isEmpty() || cipherSha256.size() != 64) {
        emit attachmentEvent(QStringLiteral("error"), {}, uploadId, commandId, 0, chunkIndex, {}, {}, {}, {},
                             QStringLiteral("invalid attachment chunk"));
        return;
    }
    message::Message message{"attachment.chunk"};
    message.upload_id = uploadId.trimmed().toStdString();
    message.chunk_index = chunkIndex;
    message.ciphertext = ciphertext.toBase64().toStdString();
    message.cipher_sha256 = cipherSha256.toLower().toStdString();
    message.command_id = commandId.trimmed().toStdString();
    const QString normalizedCommand = commandId.trimmed();
    auto transferIt = attachmentTransfers_.find(uploadId.trimmed());
    if (normalizedCommand.isEmpty() || transferIt == attachmentTransfers_.end() ||
        !transferIt->second.registerChunkCommand(normalizedCommand.toStdString(), chunkIndex)) {
        emit attachmentEvent(QStringLiteral("error"), {}, uploadId, commandId, 0, chunkIndex, {}, {}, {}, {},
                             QStringLiteral("attachment transfer is not initialized or command is duplicated"));
        return;
    }
    pendingAttachmentChunks_[normalizedCommand] = {uploadId.trimmed(), chunkIndex};
    NetworkDiagnostics::writeConnectionEvent(
        QStringLiteral("attachment_chunk_send"), savedConnection_.serverIp, savedConnection_.serverPort,
        reconnectPolicy_.attemptCount(),
        QStringLiteral("upload_id=") + uploadId.trimmed() + QStringLiteral(" chunk_index=") + QString::number(chunkIndex) +
            QStringLiteral(" total_chunks=") + QString::number(transferIt->second.chunkCount()) +
            QStringLiteral(" received_chunks=") + QString::number(transferIt->second.receivedCount()));
    if (!connection_->send(message)) {
        const QString reason = QString::fromStdString(connection_->last_error());
        pendingAttachmentChunks_.erase(normalizedCommand);
        transferIt->second.fail();
        emit attachmentEvent(QStringLiteral("error"), {}, uploadId, commandId, 0, chunkIndex, {}, {}, {}, {}, reason);
        emit connectionLost(reason);
    }
}

void GuiConnectionWorker::sendNextAttachmentChunk(const QString& uploadId) {
    const auto jobIt = attachmentUploadJobs_.find(uploadId.trimmed());
    if (jobIt == attachmentUploadJobs_.end()) return;
    auto& job = jobIt->second;
    // Keep a bounded window on the wire instead of paying one round trip per
    // 47 KiB chunk. File reads remain incremental and acknowledgements refill it.
    while (!job.lastChunkSent && job.inFlightChunks < AttachmentUploadJob::WindowSize) {
        attachments::TransferChunk chunk;
        bool endOfFile = false;
        QString error;
        if (!attachments::TransferClient::encrypt_next_chunk(*job.input, job.key, job.context,
                                                             chunk, endOfFile, &error)) {
            auto transferIt = attachmentTransfers_.find(uploadId.trimmed());
            if (transferIt != attachmentTransfers_.end()) transferIt->second.fail();
            emit attachmentEvent(QStringLiteral("error"), {}, uploadId, job.baseCommand, 0,
                                 job.context.chunk_index, {}, {}, {}, {}, error);
            attachmentUploadJobs_.erase(jobIt);
            return;
        }
        if (endOfFile && chunk.ciphertext.isEmpty()) {
            auto transferIt = attachmentTransfers_.find(uploadId.trimmed());
            if (transferIt != attachmentTransfers_.end()) transferIt->second.fail();
            emit attachmentEvent(QStringLiteral("error"), {}, uploadId, job.baseCommand, 0,
                                 job.context.chunk_index, {}, {}, {}, {},
                                 QStringLiteral("attachment file has no remaining data"));
            attachmentUploadJobs_.erase(jobIt);
            return;
        }
        job.lastChunkSent = endOfFile;
        const QString commandId = job.baseCommand + QStringLiteral("-chunk-%1").arg(chunk.index);
        ++job.context.chunk_index;
        ++job.inFlightChunks;
        sendAttachmentChunk(uploadId, chunk.index, chunk.ciphertext, chunk.cipher_sha256, commandId);
        const auto transfer = attachmentTransfers_.find(uploadId.trimmed());
        if (transfer == attachmentTransfers_.end() ||
            transfer->second.status() == attachments::AttachmentTransferState::Status::Failed) return;
    }
}

QString GuiConnectionWorker::attachmentEventCommandId(const message::Message& incoming) const {
    const QString commandId = QString::fromStdString(incoming.command_id);
    if (!commandId.isEmpty() && pendingAttachmentChunks_.find(commandId) != pendingAttachmentChunks_.end()) {
        return commandId;
    }
    if (incoming.type != "attachment.chunk") return commandId;
    for (const auto& entry : pendingAttachmentChunks_) {
        if (entry.second.first == QString::fromStdString(incoming.upload_id) &&
            entry.second.second == incoming.chunk_index) {
            return entry.first;
        }
    }
    return commandId;
}

void GuiConnectionWorker::resumeAttachment(const QString& uploadId, const QString& commandId) {
    if (!connection_ || !connection_->is_ready() || uploadId.trimmed().isEmpty()) {
        emit attachmentEvent(QStringLiteral("error"), {}, uploadId, commandId, 0, 0, {}, {}, {}, {},
                             QStringLiteral("invalid attachment resume"));
        return;
    }
    message::Message message{"attachment.resume"};
    message.upload_id = uploadId.trimmed().toStdString();
    message.command_id = commandId.trimmed().toStdString();
    if (!connection_->send(message)) {
        const QString reason = QString::fromStdString(connection_->last_error());
        emit attachmentEvent(QStringLiteral("error"), {}, uploadId, commandId, 0, 0, {}, {}, {}, {}, reason);
        emit connectionLost(reason);
    }
}

void GuiConnectionWorker::sendAttachmentCommit(const QString& uploadId, const QString& commandId) {
    if (!connection_ || !connection_->is_ready() || uploadId.trimmed().isEmpty() || commandId.trimmed().isEmpty()) {
        emit attachmentEvent(QStringLiteral("error"), {}, uploadId, commandId, 0, 0, {}, {}, {}, {},
                             QStringLiteral("invalid attachment commit"));
        return;
    }
    message::Message message{"attachment.commit"};
    message.upload_id = uploadId.trimmed().toStdString();
    message.command_id = commandId.trimmed().toStdString();
    if (!connection_->send(message)) {
        const QString reason = QString::fromStdString(connection_->last_error());
        emit attachmentEvent(QStringLiteral("error"), {}, uploadId, commandId, 0, 0, {}, {}, {}, {}, reason);
        emit connectionLost(reason);
    }
}

void GuiConnectionWorker::sendAttachmentDownload(const QString& attachmentId, qint64 chunkIndex,
                                                  const QString& commandId) {
    if (!connection_ || !connection_->is_ready() || attachmentId.trimmed().isEmpty() ||
        chunkIndex < 0 || commandId.trimmed().isEmpty()) {
        emit attachmentEvent(QStringLiteral("error"), attachmentId, {}, commandId, 0, chunkIndex,
                             {}, {}, {}, {}, QStringLiteral("invalid attachment download"));
        return;
    }
    message::Message message{"attachment.download"};
    message.attachment_id = attachmentId.trimmed().toStdString();
    message.chunk_index = chunkIndex;
    message.command_id = commandId.trimmed().toStdString();
    if (!connection_->send(message)) {
        const QString reason = QString::fromStdString(connection_->last_error());
        emit attachmentEvent(QStringLiteral("error"), attachmentId, {}, commandId, 0, chunkIndex,
                             {}, {}, {}, {}, reason);
        emit connectionLost(reason);
    }
}

void GuiConnectionWorker::startAttachmentDownload(const QString& attachmentId, const QString& outputPath,
                                                  const QString& commandId) {
#ifndef LAN_CHAT_ENABLE_MLSPP
    emit attachmentEvent(QStringLiteral("error"), attachmentId, {}, commandId, 0, 0, {}, {}, {}, {},
                         QStringLiteral("MLS++ is required before downloading an attachment"));
    return;
#else
    const auto manifestIt = receivedAttachmentManifests_.find(attachmentId.trimmed());
    if (manifestIt == receivedAttachmentManifests_.end() || outputPath.trimmed().isEmpty() ||
        commandId.trimmed().isEmpty()) {
        emit attachmentEvent(QStringLiteral("error"), attachmentId, {}, commandId, 0, 0, {}, {}, {}, {},
                             QStringLiteral("attachment manifest or output path is unavailable"));
        return;
    }
    AttachmentDownloadJob job;
    job.manifest = manifestIt->second;
    if (!attachmentKeyFromBytes(job.manifest.key, job.manifestKey)) {
        emit attachmentEvent(QStringLiteral("error"), attachmentId, {}, commandId, 0, 0, {}, {}, {}, {},
                             QStringLiteral("attachment manifest contains an invalid key"));
        return;
    }
    job.commandId = commandId.trimmed();
    attachments::ChunkContext context{job.manifest.attachmentId.toStdString(), job.manifest.room.toStdString(),
                                      job.manifest.groupId.toStdString(), job.manifest.logicalSize,
                                      job.manifest.chunkSize, 0};
    QString error;
    if (!job.assembler.begin(outputPath.trimmed(), job.manifestKey, std::move(context), &error)) {
        emit attachmentEvent(QStringLiteral("error"), attachmentId, {}, commandId, 0, 0, {}, {}, {}, {}, error);
        return;
    }
    attachmentDownloadJobs_[job.manifest.attachmentId] = std::move(job);
    sendAttachmentDownload(attachmentId, 0, commandId);
#endif
}

void GuiConnectionWorker::processAttachmentMessage(const message::Message& incoming) {
    const QString commandId = QString::fromStdString(incoming.command_id);
    if (incoming.type == "error") {
        pendingAttachmentInits_.erase(commandId);
        pendingAttachmentFiles_.erase(commandId);
        const auto chunk = pendingAttachmentChunks_.find(commandId);
        if (chunk != pendingAttachmentChunks_.end()) {
            const QString uploadId = chunk->second.first;
            const auto transfer = attachmentTransfers_.find(uploadId);
            if (transfer != attachmentTransfers_.end()) transfer->second.fail();
            attachmentUploadJobs_.erase(uploadId);
            for (auto it = pendingAttachmentChunks_.begin(); it != pendingAttachmentChunks_.end();) {
                if (it->second.first == uploadId) it = pendingAttachmentChunks_.erase(it);
                else ++it;
            }
        }
        return;
    }
    if (incoming.type == "attachment.init") {
        NetworkDiagnostics::writeConnectionEvent(QStringLiteral("attachment_init_ack"), savedConnection_.serverIp,
                                                  savedConnection_.serverPort, reconnectPolicy_.attemptCount(),
                                                  QStringLiteral("upload_id=") + QString::fromStdString(incoming.upload_id));
        const auto it = pendingAttachmentInits_.find(commandId);
        if (it == pendingAttachmentInits_.end()) return;
        auto transfer = std::move(it->second);
        pendingAttachmentInits_.erase(it);
        if (!transfer.acceptInit(incoming.attachment_id, incoming.upload_id, incoming.chunk_size,
                                 incoming.expires_at)) {
            emit attachmentEvent(QStringLiteral("error"), {}, {}, commandId, 0, 0, {}, {}, {}, {},
                                 QStringLiteral("invalid attachment initialization response"));
            return;
        }
        attachmentTransfers_[QString::fromStdString(incoming.upload_id)] = std::move(transfer);
        const auto& acceptedTransfer = attachmentTransfers_.at(QString::fromStdString(incoming.upload_id));
        NetworkDiagnostics::writeConnectionEvent(
            QStringLiteral("attachment_init_progress"), savedConnection_.serverIp, savedConnection_.serverPort,
            reconnectPolicy_.attemptCount(),
            QStringLiteral("upload_id=") + QString::fromStdString(incoming.upload_id) +
                QStringLiteral(" logical_size=") + QString::number(acceptedTransfer.logicalSize()) +
                QStringLiteral(" chunk_size=") + QString::number(acceptedTransfer.chunkSize()) +
                QStringLiteral(" total_chunks=") + QString::number(acceptedTransfer.chunkCount()) +
                QStringLiteral(" received_chunks=") + QString::number(acceptedTransfer.receivedCount()));
        const auto fileIt = pendingAttachmentFiles_.find(commandId);
        if (fileIt != pendingAttachmentFiles_.end()) {
            const PendingAttachmentFile file = fileIt->second;
            pendingAttachmentFiles_.erase(fileIt);
            auto input = std::make_shared<QFile>(file.filePath);
            if (!input->open(QIODevice::ReadOnly)) {
                emit attachmentEvent(QStringLiteral("error"), {},
                                     QString::fromStdString(incoming.upload_id), commandId, incoming.chunk_size,
                                     0, {}, {}, {}, {}, QStringLiteral("unable to open attachment file"));
                return;
            }
            AttachmentUploadJob job;
            job.room = file.room;
            job.fileName = QFileInfo(file.filePath).fileName();
            job.baseCommand = commandId;
            job.key = file.key;
            job.context = {incoming.attachment_id, file.room.toStdString(),
#ifdef LAN_CHAT_ENABLE_MLSPP
                           file.groupId.toStdString(),
#else
                           {},
#endif
                           file.logicalSize,
                           incoming.chunk_size, 0};
            job.input = std::move(input);
            attachmentUploadJobs_[QString::fromStdString(incoming.upload_id)] = std::move(job);
            sendNextAttachmentChunk(QString::fromStdString(incoming.upload_id));
        }
        return;
    }
    if (incoming.type == "attachment.resume") {
        const auto it = attachmentTransfers_.find(QString::fromStdString(incoming.upload_id));
        if (it == attachmentTransfers_.end()) return;
        std::vector<std::int64_t> indexes(incoming.received_indexes.begin(), incoming.received_indexes.end());
        it->second.acceptResume(indexes);
        NetworkDiagnostics::writeConnectionEvent(
            QStringLiteral("attachment_resume_progress"), savedConnection_.serverIp, savedConnection_.serverPort,
            reconnectPolicy_.attemptCount(),
            QStringLiteral("upload_id=") + QString::fromStdString(incoming.upload_id) +
                QStringLiteral(" total_chunks=") + QString::number(it->second.chunkCount()) +
                QStringLiteral(" received_chunks=") + QString::number(it->second.receivedCount()));
        return;
    }
    if (incoming.type == "attachment.chunk") {
        auto pending = pendingAttachmentChunks_.find(commandId);
        if (pending == pendingAttachmentChunks_.end()) {
            for (auto candidate = pendingAttachmentChunks_.begin(); candidate != pendingAttachmentChunks_.end(); ++candidate) {
                if (candidate->second.first == QString::fromStdString(incoming.upload_id) &&
                    candidate->second.second == incoming.chunk_index) {
                    pending = candidate;
                    break;
                }
            }
        }
        if (pending == pendingAttachmentChunks_.end()) return;
        const auto [uploadId, chunkIndex] = pending->second;
        const QString matchedCommandId = pending->first;
        pendingAttachmentChunks_.erase(pending);
        const auto it = attachmentTransfers_.find(uploadId);
        if (it != attachmentTransfers_.end() && it->second.acceptChunkAck(matchedCommandId.toStdString(), chunkIndex)) {
            NetworkDiagnostics::writeConnectionEvent(
                QStringLiteral("attachment_chunk_ack"), savedConnection_.serverIp, savedConnection_.serverPort,
                reconnectPolicy_.attemptCount(),
                QStringLiteral("upload_id=") + uploadId + QStringLiteral(" chunk_index=") + QString::number(chunkIndex) +
                    QStringLiteral(" total_chunks=") + QString::number(it->second.chunkCount()) +
                    QStringLiteral(" received_chunks=") + QString::number(it->second.receivedCount()));
            const auto jobIt = attachmentUploadJobs_.find(uploadId);
            if (jobIt != attachmentUploadJobs_.end()) {
                --jobIt->second.inFlightChunks;
                if (jobIt->second.lastChunkSent && jobIt->second.inFlightChunks == 0) {
                    const QString commitCommand = jobIt->second.baseCommand + QStringLiteral("-commit");
#ifdef LAN_CHAT_ENABLE_MLSPP
                    attachments::AttachmentManifest manifest;
                    manifest.attachmentId = QString::fromStdString(jobIt->second.context.attachment_id);
                    manifest.fileName = jobIt->second.fileName;
                    manifest.room = jobIt->second.room;
                    manifest.groupId = QString::fromStdString(jobIt->second.context.group_id);
                    manifest.logicalSize = jobIt->second.context.logical_size;
                    manifest.chunkSize = jobIt->second.context.chunk_size;
                    const auto groupIt = mlsGroups_.find(manifest.groupId);
                    manifest.epoch = groupIt == mlsGroups_.end() ? 0 : groupIt->second.epoch;
                    manifest.key = QByteArray(reinterpret_cast<const char*>(jobIt->second.key.data()),
                                              static_cast<int>(jobIt->second.key.size()));
                    pendingAttachmentManifests_[commitCommand] = std::move(manifest);
#endif
                    sendAttachmentCommit(uploadId, commitCommand);
                    attachmentUploadJobs_.erase(jobIt);
                } else {
                    sendNextAttachmentChunk(uploadId);
                }
            }
        }
        return;
    }
    if (incoming.type == "attachment.commit") {
#ifdef LAN_CHAT_ENABLE_MLSPP
        const auto it = pendingAttachmentManifests_.find(commandId);
        if (it == pendingAttachmentManifests_.end()) return;
        const auto manifest = std::move(it->second);
        pendingAttachmentManifests_.erase(it);
        if (incoming.attachment_id == manifest.attachmentId.toStdString()) {
            sendAttachmentManifest(manifest);
        }
#endif
    }
    if (incoming.type == "attachment.download") {
#ifdef LAN_CHAT_ENABLE_MLSPP
        const QString attachmentId = QString::fromStdString(incoming.attachment_id);
        const auto jobIt = attachmentDownloadJobs_.find(attachmentId);
        if (jobIt == attachmentDownloadJobs_.end()) return;
        const QByteArray ciphertext = QByteArray::fromBase64(QByteArray::fromStdString(incoming.ciphertext));
        QString error;
        if (!jobIt->second.assembler.acceptChunk(incoming.chunk_index, ciphertext,
                                                 QByteArray::fromStdString(incoming.cipher_sha256),
                                                 incoming.content == "last", &error)) {
            emit attachmentEvent(QStringLiteral("error"), attachmentId, {}, jobIt->second.commandId,
                                 incoming.chunk_size, incoming.chunk_index, {}, {}, {}, {}, error);
            attachmentDownloadJobs_.erase(jobIt);
            return;
        }
        const QString commandId = jobIt->second.commandId;
        if (jobIt->second.assembler.complete()) {
            attachmentDownloadJobs_.erase(jobIt);
            emit attachmentEvent(QStringLiteral("completed"), attachmentId, {}, commandId,
                                 incoming.chunk_size, incoming.chunk_index, {}, {}, {}, {}, {});
        } else {
            sendAttachmentDownload(attachmentId, incoming.chunk_index + 1, commandId);
        }
#endif
    }
}

#ifdef LAN_CHAT_ENABLE_MLSPP
void GuiConnectionWorker::sendAttachmentManifest(const attachments::AttachmentManifest& manifest) {
    const auto groupIt = mlsGroups_.find(manifest.groupId);
    const QByteArray plaintext = manifest.encode();
    if (groupIt == mlsGroups_.end() || !groupIt->second.client || plaintext.isEmpty()) {
        emit attachmentEvent(QStringLiteral("error"), manifest.attachmentId, {}, {}, 0, 0, {}, {}, {}, {},
                             QStringLiteral("unable to protect attachment manifest"));
        return;
    }
    try {
        const QByteArray ciphertext = encodeMlsOpaque(groupIt->second.client->protect(mlsBytes(plaintext)));
        if (ciphertext.isEmpty()) throw std::runtime_error("MLS attachment manifest is too large");
        const nlohmann::json envelope = {
            {"v", 1}, {"alg", "mls-v1"}, {"group_id", manifest.groupId.toStdString()},
            {"epoch", groupIt->second.epoch}, {"ciphertext", ciphertext.toStdString()}
        };
        message::Message message{"chat"};
        message.room = manifest.room.toStdString();
        message.message_id = attachments::manifestMessageId(manifest.attachmentId).toStdString();
        message.crypto = envelope.dump();
        if (!connection_->send(message)) {
            emit connectionLost(QString::fromStdString(connection_->last_error()));
        }
    } catch (const std::exception& error) {
        emit attachmentEvent(QStringLiteral("error"), manifest.attachmentId, {}, {}, 0, 0, {}, {}, {}, {},
                             QString::fromUtf8(error.what()));
    }
}
#endif

void GuiConnectionWorker::fetchMlsKeyPackage(const QString& room,
                                             const QString& targetUserCode,
                                             const QString& commandId) {
#ifndef LAN_CHAT_ENABLE_MLSPP
    emit mlsCommandResult(commandId, false, QStringLiteral("capability_unavailable"),
                          QStringLiteral("MLS++ support is not enabled"));
    return;
#else
    if (!connection_ || !connection_->is_ready()) {
        emit mlsCommandResult(commandId, false, QStringLiteral("not_connected"),
                              QStringLiteral("MLS control requires an active connection"));
        return;
    }
    if (room.trimmed().isEmpty() || targetUserCode.trimmed().isEmpty()) {
        emit mlsCommandResult(commandId, false, QStringLiteral("invalid_mls_request"),
                              QStringLiteral("MLS room and target are required"));
        return;
    }
    message::Message message{"mls.key_package.fetch", "", "", "", {},
                             targetUserCode.trimmed().toStdString(), room.trimmed().toStdString(), {}, ""};
    message.protocol_version = "mls-v1";
    message.command_id = commandId.trimmed().toStdString();
    pendingMlsCommands_.insert(commandId);
    if (!connection_->send(message)) {
        pendingMlsCommands_.remove(commandId);
        emit mlsCommandResult(commandId, false, QStringLiteral("send_failed"),
                              QString::fromStdString(connection_->last_error()));
        emit connectionLost(QString::fromStdString(connection_->last_error()));
    }
#endif
}

void GuiConnectionWorker::addMlsMember(const QString& room, const QString& groupId,
                                       const QString& targetUserCode, const QString& commandId) {
#ifndef LAN_CHAT_ENABLE_MLSPP
    emit mlsCommandResult(commandId, false, QStringLiteral("capability_unavailable"),
                          QStringLiteral("MLS++ support is not enabled"));
    return;
#else
    if (!connection_ || !connection_->is_ready()) {
        emit mlsCommandResult(commandId, false, QStringLiteral("not_connected"),
                              QStringLiteral("MLS control requires an active connection"));
        return;
    }
    const QString normalizedRoom = room.trimmed();
    const QString normalizedGroup = groupId.trimmed();
    const QString normalizedTarget = targetUserCode.trimmed().toLower();
    if (normalizedRoom.isEmpty() || normalizedGroup.isEmpty() || normalizedTarget.isEmpty()) {
        emit mlsCommandResult(commandId, false, QStringLiteral("invalid_mls_request"),
                              QStringLiteral("MLS room, group and target are required"));
        return;
    }
    const auto keyPackageIt = mlsKeyPackages_.find(normalizedTarget);
    if (keyPackageIt == mlsKeyPackages_.end()) {
        emit mlsCommandResult(commandId, false, QStringLiteral("key_package_unavailable"),
                              QStringLiteral("Fetch the target key package before adding a member"));
        return;
    }

    try {
        auto groupIt = mlsGroups_.find(normalizedGroup);
        if (groupIt == mlsGroups_.end()) {
            MlsGroupState state;
            state.client = std::make_shared<MlsClient>(MlsClient::create(mlsIdentity(savedConnection_.userCode)));
            state.client->createGroup(mlsBytes(normalizedGroup.toUtf8()));
            state.room = normalizedRoom;
            groupIt = mlsGroups_.emplace(normalizedGroup, std::move(state)).first;
        } else if (groupIt->second.room != normalizedRoom) {
            emit mlsCommandResult(commandId, false, QStringLiteral("group_room_mismatch"),
                                  QStringLiteral("MLS group is already bound to another room"));
            return;
        }

        auto& state = groupIt->second;
        const auto proposal = state.client->addMember(decodeMlsOpaque(keyPackageIt->second.toStdString()));
        const auto proposalEncoded = encodeMlsOpaque(proposal);
        const auto commit = state.client->commit();
        const auto commitEncoded = encodeMlsOpaque(commit.handshake);
        const auto welcomeEncoded = encodeMlsOpaque(commit.welcome);
        if (proposalEncoded.isEmpty() || commitEncoded.isEmpty() || welcomeEncoded.isEmpty()) {
            emit mlsCommandResult(commandId, false, QStringLiteral("mls_opaque_too_large"),
                                  QStringLiteral("MLS control payload exceeds the frame limit"));
            return;
        }
        if (state.epoch == std::numeric_limits<quint64>::max()) {
            emit mlsCommandResult(commandId, false, QStringLiteral("mls_epoch_exhausted"),
                                  QStringLiteral("MLS group epoch cannot advance"));
            return;
        }
        const quint64 epoch = state.epoch + 1;

        const QString pendingId = commandId.trimmed();
        if (pendingId.isEmpty() || pendingMlsOperations_.find(pendingId) != pendingMlsOperations_.end()) {
            emit mlsCommandResult(commandId, false, QStringLiteral("command_pending"),
                                  QStringLiteral("MLS command id is already pending or empty"));
            return;
        }
        message::Message proposalMessage{"mls.group.proposal", "", "", "", {}, "",
                                         normalizedRoom.toStdString(), {}, ""};
        proposalMessage.protocol_version = "mls-v1";
        proposalMessage.group_id = normalizedGroup.toStdString();
        proposalMessage.action = "add";
        proposalMessage.target_user_code = normalizedTarget.toStdString();
        proposalMessage.epoch = epoch;
        proposalMessage.proposal_id = pendingId.toStdString();
        proposalMessage.proposal = proposalEncoded.toStdString();
        proposalMessage.command_id = pendingId.toStdString();
        pendingMlsOperations_.emplace(pendingId, PendingMlsOperation{PendingMlsOperation::Phase::Proposal,
                                                                       normalizedRoom, normalizedGroup, normalizedTarget,
                                                                       epoch, proposalEncoded, commitEncoded, welcomeEncoded, true});
        pendingMlsCommands_.insert(commandId);
        if (!connection_->send(proposalMessage)) {
            pendingMlsOperations_.erase(pendingId);
            pendingMlsCommands_.remove(commandId);
            emit mlsCommandResult(commandId, false, QStringLiteral("send_failed"),
                                  QString::fromStdString(connection_->last_error()));
            return;
        }
    } catch (const std::exception& error) {
        emit mlsCommandResult(commandId, false, QStringLiteral("mls_operation_failed"),
                              QString::fromUtf8(error.what()));
    }
#endif
}

void GuiConnectionWorker::removeMlsMember(const QString& room, const QString& groupId,
                                          const QString& targetUserCode, const QString& commandId) {
#ifndef LAN_CHAT_ENABLE_MLSPP
    emit mlsCommandResult(commandId, false, QStringLiteral("capability_unavailable"),
                          QStringLiteral("MLS++ support is not enabled"));
    return;
#else
    if (!connection_ || !connection_->is_ready()) {
        emit mlsCommandResult(commandId, false, QStringLiteral("not_connected"),
                              QStringLiteral("MLS control requires an active connection"));
        return;
    }
    const QString normalizedRoom = room.trimmed();
    const QString normalizedGroup = groupId.trimmed();
    const QString normalizedTarget = targetUserCode.trimmed();
    auto groupIt = mlsGroups_.find(normalizedGroup);
    if (normalizedRoom.isEmpty() || normalizedGroup.isEmpty() || normalizedTarget.isEmpty() ||
        groupIt == mlsGroups_.end()) {
        emit mlsCommandResult(commandId, false, QStringLiteral("invalid_mls_request"),
                              QStringLiteral("MLS room, group and target are required"));
        return;
    }
    if (groupIt->second.room != normalizedRoom) {
        emit mlsCommandResult(commandId, false, QStringLiteral("group_room_mismatch"),
                              QStringLiteral("MLS group is already bound to another room"));
        return;
    }
    try {
        auto& state = groupIt->second;
        const auto proposal = state.client->removeMember(mlsIdentity(normalizedTarget));
        const auto proposalEncoded = encodeMlsOpaque(proposal);
        const auto commit = state.client->commit();
        const auto commitEncoded = encodeMlsOpaque(commit.handshake);
        if (proposalEncoded.isEmpty() || commitEncoded.isEmpty()) {
            emit mlsCommandResult(commandId, false, QStringLiteral("mls_opaque_too_large"),
                                  QStringLiteral("MLS control payload exceeds the frame limit"));
            return;
        }
        if (state.epoch == std::numeric_limits<quint64>::max()) {
            emit mlsCommandResult(commandId, false, QStringLiteral("mls_epoch_exhausted"),
                                  QStringLiteral("MLS group epoch cannot advance"));
            return;
        }
        const quint64 epoch = state.epoch + 1;
        const QString pendingId = commandId.trimmed();
        if (pendingId.isEmpty() || pendingMlsOperations_.find(pendingId) != pendingMlsOperations_.end()) {
            emit mlsCommandResult(commandId, false, QStringLiteral("command_pending"),
                                  QStringLiteral("MLS command id is already pending or empty"));
            return;
        }
        message::Message proposalMessage{"mls.group.proposal", "", "", "", {}, "",
                                         normalizedRoom.toStdString(), {}, ""};
        proposalMessage.protocol_version = "mls-v1";
        proposalMessage.group_id = normalizedGroup.toStdString();
        proposalMessage.action = "remove";
        proposalMessage.target_user_code = normalizedTarget.toStdString();
        proposalMessage.epoch = epoch;
        proposalMessage.proposal_id = pendingId.toStdString();
        proposalMessage.proposal = proposalEncoded.toStdString();
        proposalMessage.command_id = pendingId.toStdString();
        pendingMlsOperations_.emplace(pendingId, PendingMlsOperation{PendingMlsOperation::Phase::Proposal,
                                                                       normalizedRoom, normalizedGroup, normalizedTarget,
                                                                       epoch, proposalEncoded, commitEncoded, {}, false});
        pendingMlsCommands_.insert(commandId);
        if (!connection_->send(proposalMessage)) {
            pendingMlsOperations_.erase(pendingId);
            pendingMlsCommands_.remove(commandId);
            emit mlsCommandResult(commandId, false, QStringLiteral("send_failed"),
                                  QString::fromStdString(connection_->last_error()));
            return;
        }
    } catch (const std::exception& error) {
        emit mlsCommandResult(commandId, false, QStringLiteral("mls_operation_failed"),
                              QString::fromUtf8(error.what()));
    }
#endif
}

void GuiConnectionWorker::resetMlsState() {
#ifdef LAN_CHAT_ENABLE_MLSPP
    mlsKeyPackages_.clear();
    mlsGroups_.clear();
    pendingMlsOperations_.clear();
    pendingMlsCommands_.clear();
    try {
        mlsClient_ = std::make_shared<MlsClient>(MlsClient::create(mlsIdentity(savedConnection_.userCode)));
    } catch (...) {
        mlsClient_.reset();
    }
#endif
}

void GuiConnectionWorker::inspectMlsGroup(const QString& groupId, const QString& commandId) {
#ifndef LAN_CHAT_ENABLE_MLSPP
    emit mlsGroupState(commandId, false, groupId, 0, QStringLiteral("MLS++ support is not enabled"));
#else
    const auto it = mlsGroups_.find(groupId.trimmed());
    if (it == mlsGroups_.end() || !it->second.client) {
        emit mlsGroupState(commandId, false, groupId, 0, QStringLiteral("MLS group is not initialized"));
        return;
    }
    emit mlsGroupState(commandId, true, it->first, it->second.epoch, {});
#endif
}

void GuiConnectionWorker::protectMls(const QString& groupId, const QByteArray& plaintext,
                                     const QString& commandId) {
#ifndef LAN_CHAT_ENABLE_MLSPP
    emit mlsDataResult(commandId, false, {}, QStringLiteral("MLS++ support is not enabled"));
#else
    try {
        const auto it = mlsGroups_.find(groupId.trimmed());
        if (it == mlsGroups_.end() || !it->second.client) {
            emit mlsDataResult(commandId, false, {}, QStringLiteral("MLS group is not initialized"));
            return;
        }
        const auto ciphertext = encodeMlsOpaque(it->second.client->protect(mlsBytes(plaintext)));
        if (ciphertext.isEmpty()) {
            emit mlsDataResult(commandId, false, {}, QStringLiteral("MLS ciphertext exceeds the frame limit"));
            return;
        }
        emit mlsDataResult(commandId, true, ciphertext, {});
    } catch (const std::exception& error) {
        emit mlsDataResult(commandId, false, {}, QString::fromUtf8(error.what()));
    }
#endif
}

void GuiConnectionWorker::unprotectMls(const QString& groupId, const QByteArray& ciphertext,
                                       const QString& commandId) {
#ifndef LAN_CHAT_ENABLE_MLSPP
    emit mlsDataResult(commandId, false, {}, QStringLiteral("MLS++ support is not enabled"));
#else
    try {
        const auto it = mlsGroups_.find(groupId.trimmed());
        if (it == mlsGroups_.end() || !it->second.client) {
            emit mlsDataResult(commandId, false, {}, QStringLiteral("MLS group is not initialized"));
            return;
        }
        const auto plaintext = it->second.client->unprotect(decodeMlsOpaque(ciphertext.toStdString()));
        emit mlsDataResult(commandId, true,
                           QByteArray(reinterpret_cast<const char*>(plaintext.data()), static_cast<int>(plaintext.size())), {});
    } catch (const std::exception& error) {
        emit mlsDataResult(commandId, false, {}, QString::fromUtf8(error.what()));
    }
#endif
}

#if defined(LAN_CHAT_ENABLE_MLSPP) && defined(LAN_CHAT_ENABLE_TEST_HOOKS)
void GuiConnectionWorker::enableDropNextMlsCommitForTesting() {
    dropNextMlsCommitForTesting_ = true;
}
#endif

void GuiConnectionWorker::publishMlsKeyPackage() {
#ifdef LAN_CHAT_ENABLE_MLSPP
    if (!connection_ || !connection_->is_ready() || !mlsClient_) {
        return;
    }
    try {
        const auto encoded = encodeMlsOpaque(mlsClient_->keyPackage());
        if (encoded.isEmpty()) {
            emit connectionLost(QStringLiteral("MLS key package exceeds the frame limit"));
            return;
        }
        message::Message message{"mls.key_package.publish", "", savedConnection_.userCode.toStdString(), "", {}, "", "", {}, ""};
        message.protocol_version = "mls-v1";
        message.key_package = encoded.toStdString();
        if (!connection_->send(message)) {
            emit connectionLost(QString::fromStdString(connection_->last_error()));
        }
    } catch (const std::exception& error) {
        emit connectionLost(QStringLiteral("MLS key package initialization failed: ") + QString::fromUtf8(error.what()));
    }
#endif
}

void GuiConnectionWorker::resumePendingMlsOperations() {
#ifdef LAN_CHAT_ENABLE_MLSPP
    if (!connection_ || !connection_->is_ready()) return;
    for (auto it = pendingMlsOperations_.begin(); it != pendingMlsOperations_.end();) {
        const QString commandId = it->first;
        const auto& operation = it->second;
        // A welcome is durably stored before its acknowledgement is sent.
        // Let the target's authenticated login path replay it rather than
        // generating a second delivery from a reconnecting sender.
        if (operation.phase == PendingMlsOperation::Phase::Welcome) {
            pendingMlsCommands_.remove(commandId);
            it = pendingMlsOperations_.erase(it);
            continue;
        }
        message::Message resumed;
        resumed.protocol_version = "mls-v1";
        resumed.group_id = operation.group.toStdString();
        resumed.room = operation.room.toStdString();
        resumed.epoch = operation.epoch;
        resumed.proposal_id = commandId.toStdString();
        resumed.command_id = commandId.toStdString();
        if (operation.phase == PendingMlsOperation::Phase::Proposal) {
            resumed.type = "mls.group.proposal";
            resumed.action = operation.add ? "add" : "remove";
            resumed.target_user_code = operation.target.toStdString();
            resumed.proposal = operation.proposal.toStdString();
        } else {
            resumed.type = "mls.group.commit";
            resumed.commit = operation.commit.toStdString();
        }
        if (!connection_->send(resumed)) {
            emit mlsCommandResult(commandId, false, QStringLiteral("send_failed"),
                                  QString::fromStdString(connection_->last_error()));
        }
        ++it;
    }
#endif
}

void GuiConnectionWorker::processMlsMessage(const message::Message& incoming) {
#ifdef LAN_CHAT_ENABLE_MLSPP
    try {
        if (incoming.type == "chat" && !incoming.crypto.empty()) {
            const nlohmann::json envelope = nlohmann::json::parse(incoming.crypto);
            if (envelope.value("v", 0) != 1 || envelope.value("alg", "") != "mls-v1" ||
                !envelope.contains("group_id") || !envelope.at("group_id").is_string() ||
                !envelope.contains("ciphertext") || !envelope.at("ciphertext").is_string()) {
                return;
            }
            const QString groupId = QString::fromStdString(envelope.at("group_id").get<std::string>());
            const auto groupIt = mlsGroups_.find(groupId);
            if (groupIt == mlsGroups_.end() || !groupIt->second.client) return;
            const auto plaintext = groupIt->second.client->unprotect(
                decodeMlsOpaque(envelope.at("ciphertext").get<std::string>()));
            const QByteArray manifestBytes(reinterpret_cast<const char*>(plaintext.data()),
                                           static_cast<int>(plaintext.size()));
            attachments::AttachmentManifest manifest;
            if (!attachments::AttachmentManifest::decode(manifestBytes, manifest) ||
                manifest.room != QString::fromStdString(incoming.room) || manifest.groupId != groupId) {
                return;
            }
            receivedAttachmentManifests_[manifest.attachmentId] = manifest;
            const QJsonObject publicMetadata{
                {QStringLiteral("fileName"), manifest.fileName},
                {QStringLiteral("logicalSize"), manifest.logicalSize},
                {QStringLiteral("chunkSize"), manifest.chunkSize},
                {QStringLiteral("totalChunks"),
                 (manifest.logicalSize + manifest.chunkSize - 1) / manifest.chunkSize}
            };
            emit attachmentEvent(QStringLiteral("manifest"), manifest.attachmentId, {},
                                 QString::fromStdString(incoming.message_id), manifest.chunkSize, 0,
                                 {}, {}, {}, {},
                                 QString::fromUtf8(QJsonDocument(publicMetadata).toJson(QJsonDocument::Compact)));
            return;
        }
        if (incoming.type == "mls.key_package.fetch" && !incoming.key_package.empty()) {
            const QByteArray encoded = QByteArray::fromStdString(incoming.key_package);
            const QString targetUser = QString::fromStdString(incoming.target_user_code);
            mlsKeyPackages_[targetUser.toLower()] = encoded;
            const QByteArray decoded = QByteArray::fromBase64(encoded);
            emit mlsKeyPackageAvailable(targetUser,
                                        QCryptographicHash::hash(decoded, QCryptographicHash::Sha256).toHex());
            const QString commandId = QString::fromStdString(incoming.command_id);
            if (pendingMlsCommands_.remove(commandId)) {
                emit mlsCommandResult(commandId, true, {}, {});
            }
            const auto fetchIt = pendingAttachmentMlsFetches_.find(commandId);
            if (fetchIt != pendingAttachmentMlsFetches_.end()) {
                const QString attachmentCommandId = fetchIt->second;
                pendingAttachmentMlsFetches_.erase(fetchIt);
                const auto setupIt = pendingAttachmentMlsSetups_.find(attachmentCommandId);
                if (setupIt == pendingAttachmentMlsSetups_.end()) return;
                const QString addCommand = attachmentCommandId + QStringLiteral("-mls-add-%1")
                    .arg(setupIt->second.nextTarget);
                pendingAttachmentMlsAdds_[addCommand] = attachmentCommandId;
                addMlsMember(setupIt->second.room, setupIt->second.groupId, targetUser, addCommand);
            }
            return;
        }
        if (incoming.type == "mls.group.proposal" && !incoming.proposal.empty()) {
            const QString groupId = QString::fromStdString(incoming.group_id);
            const QString room = QString::fromStdString(incoming.room);
            auto groupIt = mlsGroups_.find(groupId);
            if (groupIt == mlsGroups_.end() || groupIt->second.room != room) {
                emit mlsCommandResult({}, false, QStringLiteral("group_unavailable"),
                                      QStringLiteral("MLS group is not initialized for this room"));
                return;
            }
            groupIt->second.client->handleProposal(decodeMlsOpaque(incoming.proposal));
            return;
        }
        if (incoming.type == "mls.group.proposal" && incoming.proposal.empty() &&
            !incoming.command_id.empty()) {
            const QString commandId = QString::fromStdString(incoming.command_id);
            auto operationIt = pendingMlsOperations_.find(commandId);
            if (operationIt == pendingMlsOperations_.end() ||
                operationIt->second.phase != PendingMlsOperation::Phase::Proposal) {
                return;
            }
            auto& operation = operationIt->second;
            message::Message commitMessage{"mls.group.commit", "", "", "", {}, "",
                                           operation.room.toStdString(), {}, ""};
            commitMessage.protocol_version = "mls-v1";
            commitMessage.group_id = operation.group.toStdString();
            commitMessage.epoch = operation.epoch;
            commitMessage.proposal_id = commandId.toStdString();
            commitMessage.commit = operation.commit.toStdString();
            commitMessage.command_id = commandId.toStdString();
            operation.phase = PendingMlsOperation::Phase::Commit;
            #if defined(LAN_CHAT_ENABLE_TEST_HOOKS)
            if (dropNextMlsCommitForTesting_) {
                dropNextMlsCommitForTesting_ = false;
                connection_->close_current();
                return;
            }
            #endif
            if (!connection_->send(commitMessage)) {
                pendingMlsOperations_.erase(operationIt);
                pendingMlsCommands_.remove(commandId);
                emit mlsCommandResult(commandId, false, QStringLiteral("send_failed"),
                                      QString::fromStdString(connection_->last_error()));
            }
            return;
        }
        if (incoming.type == "mls.group.welcome" && !incoming.welcome.empty()) {
            const QString groupId = QString::fromStdString(incoming.group_id);
            const QString room = QString::fromStdString(incoming.room);
            const QByteArray welcome = QByteArray::fromStdString(incoming.welcome);
            if (!incoming.target_user_code.empty() &&
                QString::fromStdString(incoming.target_user_code).compare(savedConnection_.userCode, Qt::CaseInsensitive) != 0) {
                emit mlsCommandResult({}, false, QStringLiteral("mls_welcome_target_mismatch"),
                                      QStringLiteral("MLS welcome is addressed to another member"));
                return;
            }
            const auto sendWelcomeAccept = [&]() {
                if (incoming.proposal_id.empty() || incoming.welcome.empty()) return false;
                message::Message accept{"mls.group.welcome.accept", "", savedConnection_.username.toStdString(), "", {},
                                        savedConnection_.userCode.toStdString(), room.toStdString(), {}, ""};
                accept.protocol_version = "mls-v1";
                accept.group_id = incoming.group_id;
                accept.epoch = incoming.epoch;
                accept.proposal_id = incoming.proposal_id;
                accept.welcome_digest = QCryptographicHash::hash(welcome, QCryptographicHash::Sha256).toHex().toStdString();
                accept.command_id = welcomeAcceptCommandId(groupId, incoming.epoch).toStdString();
                return connection_ && connection_->send(accept);
            };
            auto groupIt = mlsGroups_.find(groupId);
            if (groupIt != mlsGroups_.end()) {
                if (groupIt->second.room != room) {
                    emit mlsCommandResult({}, false, QStringLiteral("group_room_mismatch"),
                                          QStringLiteral("MLS group is already bound to another room"));
                    return;
                }
                const auto old = groupIt->second.welcomes.find(incoming.epoch);
                if (old != groupIt->second.welcomes.end() && old->second == welcome) {
                    emit mlsWelcomeEvent(groupId, incoming.epoch, QStringLiteral("received"));
                    if (sendWelcomeAccept()) {
                        emit mlsWelcomeEvent(groupId, incoming.epoch, QStringLiteral("accept_sent"));
                    }
                    return;
                }
                if (incoming.epoch <= groupIt->second.epoch) {
                    emit mlsCommandResult({}, false, QStringLiteral("mls_epoch_conflict"),
                                          QStringLiteral("MLS welcome epoch is not strictly increasing"));
                    return;
                }
            } else {
                if (!mlsClient_) {
                    emit mlsCommandResult({}, false, QStringLiteral("key_package_unavailable"),
                                          QStringLiteral("No pending MLS key package is available for this welcome"));
                    return;
                }
                MlsGroupState state;
                // The PendingJoin must be consumed by the exact MlsClient
                // whose key package was published after authentication. A
                // newly-created client has a different private init key even
                // when its identity is identical, so its join would reject
                // this welcome as not intended for its key package.
                state.client = mlsClient_;
                state.room = room;
                state.client->join(decodeMlsOpaque(incoming.welcome));
                state.epoch = incoming.epoch;
                state.welcomes.emplace(incoming.epoch, welcome);
                mlsGroups_.emplace(groupId, std::move(state));
                emit mlsWelcomeEvent(groupId, incoming.epoch, QStringLiteral("received"));
                if (!sendWelcomeAccept()) {
                    emit mlsCommandResult({}, false, QStringLiteral("send_failed"), QString::fromStdString(connection_->last_error()));
                    return;
                }
                emit mlsWelcomeEvent(groupId, incoming.epoch, QStringLiteral("accept_sent"));
                // Key packages are single-use. Rotate the pending client so
                // a later group invitation has a fresh package without
                // disturbing the newly-created group's session.
                mlsClient_ = std::make_shared<MlsClient>(MlsClient::create(mlsIdentity(savedConnection_.userCode)));
                publishMlsKeyPackage();
                return;
            }
            return;
        }
        if (incoming.type == "mls.group.commit" && !incoming.commit.empty()) {
            const QString groupId = QString::fromStdString(incoming.group_id);
            const QString room = QString::fromStdString(incoming.room);
            auto groupIt = mlsGroups_.find(groupId);
            if (groupIt == mlsGroups_.end() || groupIt->second.room != room) {
                emit mlsCommandResult({}, false, QStringLiteral("group_unavailable"),
                                      QStringLiteral("MLS group is not initialized for this room"));
                return;
            }
            const QByteArray commit = QByteArray::fromStdString(incoming.commit);
            const auto old = groupIt->second.commits.find(incoming.epoch);
            if (old != groupIt->second.commits.end() && old->second == commit) return;
            if (incoming.epoch != groupIt->second.epoch + 1) {
                emit mlsCommandResult({}, false, QStringLiteral("mls_epoch_conflict"),
                                      QStringLiteral("MLS commit epoch is not the next epoch"));
                return;
            }
            groupIt->second.client->handleCommit(decodeMlsOpaque(incoming.commit));
            groupIt->second.epoch = incoming.epoch;
            groupIt->second.commits.emplace(incoming.epoch, commit);
            return;
        }
        if (incoming.type == "mls.group.commit" && incoming.commit.empty() && !incoming.command_id.empty()) {
            const QString commandId = QString::fromStdString(incoming.command_id);
            auto operationIt = pendingMlsOperations_.find(commandId);
            if (operationIt != pendingMlsOperations_.end() &&
                operationIt->second.phase == PendingMlsOperation::Phase::Commit) {
                auto& operation = operationIt->second;
                auto groupIt = mlsGroups_.find(operation.group);
                if (groupIt == mlsGroups_.end()) {
                    pendingMlsOperations_.erase(operationIt);
                    pendingMlsCommands_.remove(commandId);
                    emit mlsCommandResult(commandId, false, QStringLiteral("group_unavailable"),
                                          QStringLiteral("MLS group is not initialized for this room"));
                    return;
                }
                groupIt->second.epoch = operation.epoch;
                groupIt->second.commits.emplace(operation.epoch, operation.commit);
                if (operation.add) {
                    message::Message welcome{"mls.group.welcome", "", "", "", {},
                                             operation.target.toStdString(), operation.room.toStdString(), {}, ""};
                    welcome.protocol_version = "mls-v1";
                    welcome.group_id = operation.group.toStdString();
                    welcome.epoch = operation.epoch;
                    welcome.proposal_id = commandId.toStdString();
                    welcome.welcome_digest = QCryptographicHash::hash(operation.welcome, QCryptographicHash::Sha256).toHex().toStdString();
                    welcome.welcome = operation.welcome.toStdString();
                    welcome.command_id = commandId.toStdString();
                    operation.phase = PendingMlsOperation::Phase::Welcome;
                    if (!connection_->send(welcome)) {
                        pendingMlsOperations_.erase(operationIt);
                        pendingMlsCommands_.remove(commandId);
                        emit mlsCommandResult(commandId, false, QStringLiteral("send_failed"),
                                              QString::fromStdString(connection_->last_error()));
                    }
                    return;
                }
                pendingMlsOperations_.erase(operationIt);
                pendingMlsCommands_.remove(commandId);
                const bool duplicate = incoming.content == "duplicate";
                const auto addIt = pendingAttachmentMlsAdds_.find(commandId);
                if (addIt != pendingAttachmentMlsAdds_.end()) {
                    const QString attachmentCommandId = addIt->second;
                    pendingAttachmentMlsAdds_.erase(addIt);
                    const auto setupIt = pendingAttachmentMlsSetups_.find(attachmentCommandId);
                    if (setupIt != pendingAttachmentMlsSetups_.end()) {
                        ++setupIt->second.nextTarget;
                        startNextAttachmentMlsSetup(attachmentCommandId);
                    }
                }
                emit mlsCommandResult(commandId, true, duplicate ? QStringLiteral("duplicate") : QString(), {});
                return;
            }
            if (pendingMlsCommands_.remove(commandId)) {
                const bool duplicate = incoming.content == "duplicate";
                emit mlsCommandResult(commandId, true, duplicate ? QStringLiteral("duplicate") : QString(), {});
            }
            return;
        }
        if (incoming.type == "mls.group.welcome" && incoming.welcome.empty() && !incoming.command_id.empty()) {
            const QString commandId = QString::fromStdString(incoming.command_id);
            auto operationIt = pendingMlsOperations_.find(commandId);
            if (operationIt == pendingMlsOperations_.end() ||
                operationIt->second.phase != PendingMlsOperation::Phase::Welcome) {
                return;
            }
            auto& operation = operationIt->second;
            auto groupIt = mlsGroups_.find(operation.group);
            if (groupIt != mlsGroups_.end()) {
                groupIt->second.welcomes.emplace(operation.epoch, operation.welcome);
            }
            pendingMlsOperations_.erase(operationIt);
            pendingMlsCommands_.remove(commandId);
            const bool duplicate = incoming.content == "duplicate";
            const auto addIt = pendingAttachmentMlsAdds_.find(commandId);
            if (addIt != pendingAttachmentMlsAdds_.end()) {
                const QString attachmentCommandId = addIt->second;
                pendingAttachmentMlsAdds_.erase(addIt);
                const auto setupIt = pendingAttachmentMlsSetups_.find(attachmentCommandId);
                if (setupIt != pendingAttachmentMlsSetups_.end()) {
                    ++setupIt->second.nextTarget;
                    startNextAttachmentMlsSetup(attachmentCommandId);
                }
            }
            emit mlsCommandResult(commandId, true, duplicate ? QStringLiteral("duplicate") : QString(), {});
            return;
        }
        if (incoming.type == "error" && !incoming.command_id.empty()) {
            const QString commandId = QString::fromStdString(incoming.command_id);
            const auto fetchIt = pendingAttachmentMlsFetches_.find(commandId);
            if (fetchIt != pendingAttachmentMlsFetches_.end()) {
                const QString attachmentCommandId = fetchIt->second;
                pendingAttachmentMlsFetches_.erase(fetchIt);
                failAttachmentMlsSetup(attachmentCommandId, QString::fromStdString(incoming.content));
            }
            const auto addIt = pendingAttachmentMlsAdds_.find(commandId);
            if (addIt != pendingAttachmentMlsAdds_.end()) {
                const QString attachmentCommandId = addIt->second;
                pendingAttachmentMlsAdds_.erase(addIt);
                failAttachmentMlsSetup(attachmentCommandId, QString::fromStdString(incoming.content));
            }
            pendingMlsOperations_.erase(commandId);
            if (pendingMlsCommands_.remove(commandId)) {
                emit mlsCommandResult(commandId, false, QStringLiteral("server_rejected"),
                                      QString::fromStdString(incoming.content));
            }
        }
    } catch (const std::exception& error) {
        emit mlsCommandResult(QString::fromStdString(incoming.command_id), false,
                              QStringLiteral("mls_operation_failed"), QString::fromUtf8(error.what()));
    }
#else
    Q_UNUSED(incoming);
#endif
}

void GuiConnectionWorker::receiveLoop() {
    while (running_.load()) {
        message::Message incoming;
        if (!connection_->receive(incoming)) {
            if (running_.exchange(false)) {
                const QString reason = QString::fromStdString(connection_->last_error());
                NetworkDiagnostics::writeConnectionEvent(QStringLiteral("connection_lost"),
                                                          savedConnection_.serverIp, savedConnection_.serverPort,
                                                          reconnectPolicy_.attemptCount(), reason);
                emit connectionLost(reason);
            }
            return;
        }

        QStringList users;
        for (const std::string& user : incoming.users) {
            users.append(QString::fromStdString(user));
        }
        QStringList rooms;
        for (const std::string& room : incoming.rooms) {
            rooms.append(QString::fromStdString(room));
        }
        QVariantList userDetails;
        for (const message::OnlineUser& user : incoming.user_details) {
            QVariantMap detail;
            detail.insert("displayName", QString::fromStdString(user.username));
            detail.insert("userCode", QString::fromStdString(user.user_code));
            detail.insert("room", QString::fromStdString(user.room));
            detail.insert("admin", user.is_admin);
            userDetails.append(detail);
        }
        QVariantList roomDetails;
        for (const message::RoomInfo& roomInfo : incoming.room_details) {
            QVariantMap detail;
            detail.insert("roomName", QString::fromStdString(roomInfo.name));
            detail.insert("ownerCode", QString::fromStdString(roomInfo.owner_code));
            detail.insert("private", roomInfo.is_private);
            detail.insert("canManage", roomInfo.can_manage);
            roomDetails.append(detail);
        }

        const QString attachmentCommandId = attachmentEventCommandId(incoming);
        processMlsMessage(incoming);
        processAttachmentMessage(incoming);

        if (incoming.type == "attachment.init" || incoming.type == "attachment.chunk" ||
            incoming.type == "attachment.resume" || incoming.type == "attachment.commit" ||
            incoming.type == "attachment.download" || incoming.type == "error") {
            QList<qint64> receivedIndexes;
            for (const std::int64_t index : incoming.received_indexes) {
                receivedIndexes.append(index);
            }
            // The server init response has no plaintext size. Recover it from
            // the local transfer accepted by processAttachmentMessage above.
            qint64 logicalSize = 0;
            if (incoming.type == "attachment.init") {
                const auto transfer = attachmentTransfers_.find(QString::fromStdString(incoming.upload_id));
                if (transfer != attachmentTransfers_.end()) logicalSize = transfer->second.logicalSize();
            }
            emit attachmentEvent(QString::fromStdString(incoming.type),
                                 QString::fromStdString(incoming.attachment_id),
                                 QString::fromStdString(incoming.upload_id),
                                 attachmentCommandId,
                                 incoming.chunk_size, incoming.chunk_index,
                                 QByteArray::fromBase64(QByteArray::fromStdString(incoming.ciphertext)),
                                 QByteArray::fromStdString(incoming.cipher_sha256),
                                 QString::fromStdString(incoming.expires_at), receivedIndexes,
                                 QString::fromStdString(incoming.content), logicalSize);
        }

        if (incoming.type == "history_response") {
            QVariantList historyMessages;
            for (const message::Message& historyMessage : incoming.messages) {
                QVariantMap detail;
                detail.insert("messageId", QString::fromStdString(historyMessage.message_id));
                detail.insert("displayName", QString::fromStdString(historyMessage.username));
                detail.insert("userCode", QString::fromStdString(historyMessage.user_code));
                detail.insert("content", QString::fromStdString(historyMessage.content));
                detail.insert("createdAt", QString::fromStdString(historyMessage.created_at));
                detail.insert("deliveryState", QString::fromStdString(historyMessage.delivery_state));
                detail.insert("recalled", historyMessage.recalled);
                historyMessages.append(detail);
            }
            emit historyReceived(QString::fromStdString(incoming.room),
                                 QString::fromStdString(incoming.target_user_code),
                                 incoming.is_private, historyMessages, incoming.has_more,
                                 QString::fromStdString(incoming.search_query));
            continue;
        }

        emit messageReceived(QString::fromStdString(incoming.type),
                             QString::fromStdString(incoming.message_id),
							 QString::fromStdString(incoming.command_id),
                             QString::fromStdString(incoming.username),
                             QString::fromStdString(incoming.user_code),
                             QString::fromStdString(incoming.content),
                             QString::fromStdString(incoming.room),
                             QString::fromStdString(incoming.target_user_code),
                             QString::fromStdString(incoming.delivery_state),
                             users,
                             rooms,
                             userDetails,
                             roomDetails,
                             incoming.is_admin);
    }
}

void GuiConnectionWorker::stopReceiveLoop() {
    if (!running_.exchange(false)) {
        if (receiveThread_.joinable()) {
            receiveThread_.join();
        }
        return;
    }

    if (connection_) {
        connection_->request_stop();
    }
    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
}
