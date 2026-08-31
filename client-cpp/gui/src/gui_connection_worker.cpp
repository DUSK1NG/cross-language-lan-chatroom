#include "gui_connection_worker.hpp"
#include "network_diagnostics.hpp"
#include "openssl_runtime.hpp"

#include <utility>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QVariantMap>
namespace {
constexpr int kLocalHostPort = 8888;
constexpr int kLocalHostProbeTimeoutMs = 150;
constexpr int kLocalHostStartupTimeoutMs = 4000;
constexpr int kLocalHostRetryAttempts = 8;
constexpr int kLocalHostRetryDelayMs = 150;

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
    savedConnection_ = {serverIp, serverPort, username, userCode, caFile, tlsServerName, true};
    NetworkDiagnostics::writeConnectionEvent(QStringLiteral("connect_requested"), serverIp, serverPort,
                                              0, tlsServerName.isEmpty() ? QString() : QStringLiteral("tls_name=") + tlsServerName);
    connectToServerWithRetries(serverIp, serverPort, username, userCode, caFile, tlsServerName, 1);
}

bool GuiConnectionWorker::connectToServerWithRetries(const QString& serverIp,
                                                      int serverPort,
                                                      const QString& username,
                                                      const QString& userCode,
                                                      const QString& caFile,
                                                      const QString& tlsServerName,
                                                      const int attempts,
                                                      const bool reportFailure) {
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
            resetMlsState();
            publishMlsKeyPackage();
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
    QTimer::singleShot(delayMs, this, [this]() {
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
                                   savedConnection_.caFile, savedConnection_.tlsServerName, 1, false)) {
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

void GuiConnectionWorker::sendChat(const QString& content, const QString& messageId) {
    sendChatToRoom(content, QStringLiteral("lobby"), messageId);
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
        proposalMessage.epoch = epoch;
        proposalMessage.proposal_id = pendingId.toStdString();
        proposalMessage.proposal = proposalEncoded.toStdString();
        proposalMessage.command_id = pendingId.toStdString();
        pendingMlsOperations_.emplace(pendingId, PendingMlsOperation{PendingMlsOperation::Phase::Proposal,
                                                                       normalizedRoom, normalizedGroup, normalizedTarget,
                                                                       epoch, commitEncoded, welcomeEncoded, true});
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
        proposalMessage.epoch = epoch;
        proposalMessage.proposal_id = pendingId.toStdString();
        proposalMessage.proposal = proposalEncoded.toStdString();
        proposalMessage.command_id = pendingId.toStdString();
        pendingMlsOperations_.emplace(pendingId, PendingMlsOperation{PendingMlsOperation::Phase::Proposal,
                                                                       normalizedRoom, normalizedGroup, normalizedTarget,
                                                                       epoch, commitEncoded, {}, false});
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

void GuiConnectionWorker::processMlsMessage(const message::Message& incoming) {
#ifdef LAN_CHAT_ENABLE_MLSPP
    try {
        if (incoming.type == "mls.key_package.fetch" && !incoming.key_package.empty()) {
            mlsKeyPackages_[QString::fromStdString(incoming.target_user_code).toLower()] = QByteArray::fromStdString(incoming.key_package);
            const QString commandId = QString::fromStdString(incoming.command_id);
            if (pendingMlsCommands_.remove(commandId)) {
                emit mlsCommandResult(commandId, true, {}, {});
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
            commitMessage.commit = operation.commit.toStdString();
            commitMessage.command_id = commandId.toStdString();
            operation.phase = PendingMlsOperation::Phase::Commit;
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
            auto groupIt = mlsGroups_.find(groupId);
            if (groupIt != mlsGroups_.end()) {
                if (groupIt->second.room != room) {
                    emit mlsCommandResult({}, false, QStringLiteral("group_room_mismatch"),
                                          QStringLiteral("MLS group is already bound to another room"));
                    return;
                }
                const auto old = groupIt->second.welcomes.find(incoming.epoch);
                if (old != groupIt->second.welcomes.end() && old->second == welcome) return;
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
            emit mlsCommandResult(commandId, true, duplicate ? QStringLiteral("duplicate") : QString(), {});
            return;
        }
        if (incoming.type == "error" && !incoming.command_id.empty()) {
            const QString commandId = QString::fromStdString(incoming.command_id);
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

        processMlsMessage(incoming);

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
