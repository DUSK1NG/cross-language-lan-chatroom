#include "gui_connection_worker.hpp"

#include <utility>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QThread>

GuiConnectionWorker::GuiConnectionWorker(QObject* parent) : QObject(parent) {}

GuiConnectionWorker::~GuiConnectionWorker() {
    stopReceiveLoop();
}

void GuiConnectionWorker::connectToServer(const QString& serverIp,
                                          int serverPort,
                                          const QString& username,
                                          const QString& userCode,
                                          const QString& password,
                                          const QString& caFile,
                                          bool registerAccount) {
    stopReceiveLoop();

    connection::Config config{
        serverIp.toStdString(),
        serverPort,
        username.toStdString(),
        userCode.toStdString(),
        caFile.toStdString(),
        password.toStdString(),
        registerAccount,
    };
    connection_ = std::make_unique<connection::ConnectionState>(std::move(config));

    message::Message loginResponse;
    connection::LoginResult loginResult = connection::LoginResult::kRetryableFailure;
    if (!connection_->connect_and_login(loginResponse, loginResult)) {
        const QString reason = QString::fromStdString(connection_->last_error());
        connection_.reset();
        if (loginResult == connection::LoginResult::kRejected) {
            emit connectionFailed(QStringLiteral("Login rejected: ") + reason);
        } else {
            emit connectionFailed(reason);
        }
        return;
    }

    running_.store(true);
    receiveThread_ = std::thread(&GuiConnectionWorker::receiveLoop, this);
    emit connected(loginResponse.is_admin);
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

    if (!QFileInfo::exists(absoluteServerExe) || !QFileInfo::exists(absoluteCertFile) ||
        !QFileInfo::exists(absoluteKeyFile)) {
        emit connectionFailed(QStringLiteral("本地 Go Server、证书或密钥文件不存在，请检查 Host 路径"));
        return;
    }

    hostProcess_ = std::make_unique<QProcess>();
    hostProcess_->setProgram(absoluteServerExe);
    hostProcess_->setWorkingDirectory(QFileInfo(absoluteServerExe).absolutePath());
    hostProcess_->setArguments({"-cert", absoluteCertFile, "-key", absoluteKeyFile, "-db", absoluteDbFile,
                                "-admin-code", userCode});
    hostProcess_->start();
    if (!hostProcess_->waitForStarted(3000)) {
        emit connectionFailed(QStringLiteral("无法启动本地 Go Server：") + hostProcess_->errorString());
        hostProcess_.reset();
        return;
    }
    QThread::msleep(700);
    if (hostProcess_->state() != QProcess::Running) {
        const QString error = QString::fromLocal8Bit(hostProcess_->readAllStandardError()).trimmed();
        emit connectionFailed(QStringLiteral("本地 Go Server 启动后退出") +
                              (error.isEmpty() ? QString() : QStringLiteral("：") + error));
        hostProcess_.reset();
        return;
    }
    connectToServer(QStringLiteral("127.0.0.1"), 8888, username, userCode, {}, absoluteCertFile, false);
}

void GuiConnectionWorker::disconnectFromServer() {
    stopReceiveLoop();
    connection_.reset();
    if (hostProcess_) {
        hostProcess_->terminate();
        if (!hostProcess_->waitForFinished(1500)) hostProcess_->kill();
        hostProcess_.reset();
    }
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

void GuiConnectionWorker::sendAdminAction(const QString& action, const QString& targetUserCode, const QString& messageId) {
    if (!connection_ || !connection_->is_ready() || action.trimmed().isEmpty() ||
        (action.trimmed() != QStringLiteral("recall") && targetUserCode.trimmed().isEmpty())) {
        return;
    }
    const message::Message message{
        "admin_action", "", "", action.trimmed().toStdString(), {},
        targetUserCode.trimmed().toStdString(), "", {}, "", messageId.trimmed().toStdString()};
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

        emit messageReceived(QString::fromStdString(incoming.type),
                             QString::fromStdString(incoming.message_id),
                             QString::fromStdString(incoming.username),
                             QString::fromStdString(incoming.user_code),
                             QString::fromStdString(incoming.content),
                             QString::fromStdString(incoming.room),
                             QString::fromStdString(incoming.target_user_code),
                             users,
                             rooms,
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
