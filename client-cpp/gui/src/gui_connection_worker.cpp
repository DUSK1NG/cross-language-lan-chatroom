#include "gui_connection_worker.hpp"

#include <utility>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTcpSocket>
#include <QThread>
#include <QVariantMap>

namespace {
constexpr int kLocalHostPort = 8888;
constexpr int kLocalHostProbeTimeoutMs = 150;
constexpr int kLocalHostStartupTimeoutMs = 4000;
constexpr int kLocalHostRetryAttempts = 8;
constexpr int kLocalHostRetryDelayMs = 150;
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
                                          const QString& caFile) {
    stopReceiveLoop();

    connectToServerWithRetries(serverIp, serverPort, username, userCode, caFile, 1);
}

bool GuiConnectionWorker::connectToServerWithRetries(const QString& serverIp,
                                                      int serverPort,
                                                      const QString& username,
                                                      const QString& userCode,
                                                      const QString& caFile,
                                                      const int attempts) {
    const int boundedAttempts = qMax(1, attempts);
    QString lastReason;
    connection::LoginResult loginResult = connection::LoginResult::kRetryableFailure;

    for (int attempt = 0; attempt < boundedAttempts; ++attempt) {
        connection::Config config{
            serverIp.toStdString(),
            serverPort,
            username.toStdString(),
            userCode.toStdString(),
            caFile.toStdString(),
        };
        connection_ = std::make_unique<connection::ConnectionState>(std::move(config));

        message::Message loginResponse;
        if (connection_->connect_and_login(loginResponse, loginResult)) {
            running_.store(true);
            receiveThread_ = std::thread(&GuiConnectionWorker::receiveLoop, this);
            emit connected(loginResponse.is_admin);
            return true;
        }

        lastReason = QString::fromStdString(connection_->last_error());
        connection_.reset();
        if (loginResult == connection::LoginResult::kRejected) {
            emit connectionFailed(QStringLiteral("Login rejected: ") + lastReason);
            return true;
        }
        if (attempt + 1 < boundedAttempts) {
            QThread::msleep(kLocalHostRetryDelayMs);
        }
    }

    emit connectionFailed(lastReason.isEmpty() ? QStringLiteral("Connection failed") : lastReason);
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

    if (!QFileInfo::exists(absoluteServerExe)) {
        emit connectionFailed(QStringLiteral("本地 Go Server 文件不存在，请检查 Host 路径"));
        return;
    }

    const bool localIdentityReady = QFileInfo::exists(absoluteCertFile) && QFileInfo::exists(absoluteKeyFile);
    if (isLocalServerListening(kLocalHostProbeTimeoutMs) && localIdentityReady) {
        connectToServerWithRetries(QStringLiteral("127.0.0.1"), kLocalHostPort,
                                   username, userCode, absoluteCertFile, 2);
        return;
    }
    if (isLocalServerListening(kLocalHostProbeTimeoutMs)) {
        emit connectionFailed(QStringLiteral("Local port 8888 is already in use before the local TLS identity was initialized."));
        return;
    }

    stopHostedServer();
    hostProcess_ = std::make_unique<QProcess>();
    hostProcess_->setProcessChannelMode(QProcess::SeparateChannels);
    hostProcess_->setProgram(absoluteServerExe);
    hostProcess_->setWorkingDirectory(QFileInfo(absoluteServerExe).absolutePath());
    hostProcess_->setArguments({"-cert", absoluteCertFile, "-key", absoluteKeyFile, "-auto-cert", "-db", absoluteDbFile,
                                "-admin-code", userCode});
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
                               username, userCode, absoluteCertFile, kLocalHostRetryAttempts);
}

void GuiConnectionWorker::disconnectFromServer() {
    stopReceiveLoop();
    connection_.reset();
    stopHostedServer();
    emit disconnected();
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
    if (hostProcess_->state() != QProcess::NotRunning) {
        hostProcess_->terminate();
        if (!hostProcess_->waitForFinished(1500)) {
            hostProcess_->kill();
            hostProcess_->waitForFinished(500);
        }
    }
    hostProcess_.reset();
}

void GuiConnectionWorker::sendChat(const QString& content) {
    sendChatToRoom(content, QStringLiteral("lobby"));
}

void GuiConnectionWorker::sendChatToRoom(const QString& content, const QString& room) {
    if (!connection_ || !connection_->is_ready() || content.trimmed().isEmpty()) {
        return;
    }
    const message::Message message{
        "chat", "", "", content.trimmed().toStdString(), {}, "", room.toStdString(), {}, ""};
    if (!connection_->send(message)) {
        emit connectionLost(QString::fromStdString(connection_->last_error()));
    }
}

void GuiConnectionWorker::sendPrivate(const QString& content, const QString& targetUserCode) {
    if (!connection_ || !connection_->is_ready() || content.trimmed().isEmpty() || targetUserCode.isEmpty()) {
        return;
    }
    const message::Message message{
        "private_chat", "", "", content.trimmed().toStdString(), {},
        targetUserCode.toStdString(), "", {}, ""};
    if (!connection_->send(message)) {
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
                                         bool isPrivate, const QString& beforeMessageId, int limit) {
    if (!connection_ || !connection_->is_ready()) return;
    message::Message message{"history_request", "", "", "", {},
                             targetUserCode.trimmed().toStdString(), room.trimmed().toStdString(), {}, ""};
    message.is_private = isPrivate;
    message.before_message_id = beforeMessageId.trimmed().toStdString();
    message.limit = qBound(1, limit, 100);
    if (!connection_->send(message)) {
        emit connectionLost(QString::fromStdString(connection_->last_error()));
    }
}

void GuiConnectionWorker::sendAdminAction(const QString& action, const QString& targetUserCode, const QString& messageId, const QString& commandId) {
    if (!connection_ || !connection_->is_ready() || action.trimmed().isEmpty() ||
        (action.trimmed() != QStringLiteral("recall") && targetUserCode.trimmed().isEmpty())) {
        return;
    }
    const message::Message message{
        "admin_action", "", "", action.trimmed().toStdString(), {},
        targetUserCode.trimmed().toStdString(), "", {}, "", messageId.trimmed().toStdString(), commandId.trimmed().toStdString()};
    if (!connection_->send(message)) {
        emit connectionLost(QString::fromStdString(connection_->last_error()));
    }
}

void GuiConnectionWorker::receiveLoop() {
    while (running_.load()) {
        message::Message incoming;
        if (!connection_->receive(incoming)) {
            if (running_.exchange(false)) {
                emit connectionLost(QString::fromStdString(connection_->last_error()));
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

        if (incoming.type == "history_response") {
            QVariantList historyMessages;
            for (const message::Message& historyMessage : incoming.messages) {
                QVariantMap detail;
                detail.insert("messageId", QString::fromStdString(historyMessage.message_id));
                detail.insert("displayName", QString::fromStdString(historyMessage.username));
                detail.insert("userCode", QString::fromStdString(historyMessage.user_code));
                detail.insert("content", QString::fromStdString(historyMessage.content));
                detail.insert("createdAt", QString::fromStdString(historyMessage.created_at));
                detail.insert("recalled", historyMessage.recalled);
                historyMessages.append(detail);
            }
            emit historyReceived(QString::fromStdString(incoming.room),
                                 QString::fromStdString(incoming.target_user_code),
                                 incoming.is_private, historyMessages, incoming.has_more);
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
