#include "gui_chat_controller.hpp"

#include "gui_connection_worker.hpp"

#include <QDateTime>
#include <QGuiApplication>
#include <QClipboard>

namespace {
const QStringList kMessageRoles = {"messageId", "displayName", "userCode", "time", "content", "selfMessage", "systemMessage"};
}

GuiChatController::GuiChatController(QObject* parent)
    : QObject(parent),
      roomModel_(new ChatListModel({"roomName", "memberCount", "unreadCount"}, this)),
      directMessageModel_(new ChatListModel({"displayName", "userCode", "unreadCount"}, this)),
      messageModel_(new ChatListModel(kMessageRoles, this)),
    memberModel_(new ChatListModel({"displayName", "userCode", "online", "admin"}, this)),
      worker_(new GuiConnectionWorker) {
    conversationModels_.insert("room:lobby", messageModel_);


    #if 0
    messageModel_->append({{"messageId", "legacy-1"}, {"displayName", "Alice"}, {"userCode", "A001"}, {"time", "18:24"},
                           {"content", "今天的学习资料整理好了吗？"}, {"selfMessage", false}, {"systemMessage", false}});
    messageModel_->append({{"messageId", "legacy-2"}, {"displayName", "Bob"}, {"userCode", "B002"}, {"time", "18:25"},
                           {"content", "我已经完成了，可以发到这里。"}, {"selfMessage", false}, {"systemMessage", false}});
    messageModel_->append({{"messageId", "legacy-3"}, {"displayName", "Local User"}, {"userCode", "A001"}, {"time", "18:26"},
                           {"content", "好的，谢谢！"}, {"selfMessage", true}, {"systemMessage", false}});

    #endif
    refreshTimer_.setInterval(3000);
    connect(&refreshTimer_, &QTimer::timeout, this, [this]() {
        if (connected_) {
            requestUsers();
            requestRooms();
        }
    });

    worker_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &GuiConnectionWorker::connected, this, &GuiChatController::handleConnected);
    connect(worker_, &GuiConnectionWorker::connectionFailed, this, &GuiChatController::handleConnectionFailed);
    connect(worker_, &GuiConnectionWorker::connectionLost, this, &GuiChatController::handleConnectionLost);
    connect(worker_, &GuiConnectionWorker::messageReceived, this, &GuiChatController::handleMessage);
    workerThread_.start();
}

GuiChatController::~GuiChatController() {
    disconnectFromServer();
    workerThread_.quit();
    workerThread_.wait();
}

void GuiChatController::connectToServer(const QString& serverIp, int serverPort,
                                        const QString& username, const QString& userCode,
                                        const QString& password, const QString& caFile,
                                        bool registerAccount) {
    const bool identityChanged = localUserName_ != username || localUserCode_ != userCode;
    localUserName_ = username;
    localUserCode_ = userCode;
    if (identityChanged) emit localIdentityChanged();
    setStatus(QStringLiteral("正在连接..."));
    QMetaObject::invokeMethod(worker_, "connectToServer", Qt::QueuedConnection,
                              Q_ARG(QString, serverIp), Q_ARG(int, serverPort),
                              Q_ARG(QString, username), Q_ARG(QString, userCode),
                              Q_ARG(QString, password), Q_ARG(QString, caFile),
                              Q_ARG(bool, registerAccount));
}

void GuiChatController::connectToLocalHost(const QString& serverExe,
                                            const QString& certFile,
                                            const QString& keyFile,
                                            const QString& dbFile,
                                            const QString& username,
                                            const QString& userCode) {
    const bool identityChanged = localUserName_ != username || localUserCode_ != userCode;
    localUserName_ = username;
    localUserCode_ = userCode;
    if (identityChanged) emit localIdentityChanged();
    setStatus(QStringLiteral("正在启动本地 Server..."));
    QMetaObject::invokeMethod(worker_, "connectToLocalHost", Qt::QueuedConnection,
                              Q_ARG(QString, serverExe), Q_ARG(QString, certFile),
                              Q_ARG(QString, keyFile), Q_ARG(QString, dbFile),
                              Q_ARG(QString, username), Q_ARG(QString, userCode));
}

void GuiChatController::disconnectFromServer() {
    refreshTimer_.stop();
    if (worker_) {
        QMetaObject::invokeMethod(worker_, "disconnectFromServer", Qt::BlockingQueuedConnection);
    }
    if (connected_) {
        connected_ = false;
        emit connectedChanged();
    }
    setStatus(QStringLiteral("未连接"));
}

void GuiChatController::sendChatMessage(const QString& content) {
    sendRoomMessage(content, activeConversationKey_.startsWith("room:")
                                   ? activeConversationKey_.mid(5)
                                   : QStringLiteral("lobby"));
}

void GuiChatController::sendRoomMessage(const QString& content, const QString& room) {
    QMetaObject::invokeMethod(worker_, "sendChatToRoom", Qt::QueuedConnection,
                              Q_ARG(QString, content), Q_ARG(QString, room));
}

void GuiChatController::sendPrivateMessage(const QString& content, const QString& targetUserCode) {
    QMetaObject::invokeMethod(worker_, "sendPrivate", Qt::QueuedConnection,
                              Q_ARG(QString, content), Q_ARG(QString, targetUserCode));
}

void GuiChatController::requestUsers() {
    QMetaObject::invokeMethod(worker_, "requestUsers", Qt::QueuedConnection);
}

void GuiChatController::requestRooms() {
    QMetaObject::invokeMethod(worker_, "requestRooms", Qt::QueuedConnection);
}

void GuiChatController::selectRoom(const QString& room) {
    const QString key = "room:" + room;
    messageModel_ = ensureConversationModel(key);
    activeConversationKey_ = key;
    const int row = roomModel_->findRow("roomName", room);
    if (row >= 0) roomModel_->updateRow(row, {{"unreadCount", 0}});
    emit activeMessageModelChanged();
    if (connected_) {
        if (joinedRoom_.compare(room, Qt::CaseInsensitive) != 0) {
            QMetaObject::invokeMethod(worker_, "joinRoom", Qt::QueuedConnection, Q_ARG(QString, room));
            joinedRoom_ = room;
        }
    }
}

void GuiChatController::selectDirectMessage(const QString& userCode) {
    const QString key = "dm:" + userCode;
    messageModel_ = ensureConversationModel(key);
    activeConversationKey_ = key;
    for (int row = 0; row < directMessageModel_->rowCount(); ++row) {
        if (directMessageModel_->valueAt(row, "userCode").toString()
                .compare(userCode, Qt::CaseInsensitive) == 0) {
            directMessageModel_->updateRow(row, {{"unreadCount", 0}});
            break;
        }
    }
    emit activeMessageModelChanged();
}

void GuiChatController::openPrivateChat(const QString& displayName, const QString& userCode) {
    if (userCode.trimmed().isEmpty() ||
        userCode.compare(localUserCode_, Qt::CaseInsensitive) == 0) {
        return;
    }

    bool found = false;
    for (int row = 0; row < directMessageModel_->rowCount(); ++row) {
        const QModelIndex index = directMessageModel_->index(row, 0);
        if (index.data(directMessageModel_->roleForName("userCode")).toString()
                .compare(userCode, Qt::CaseInsensitive) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        directMessageModel_->append({{"displayName", displayName},
                                     {"userCode", userCode},
                                     {"unreadCount", 0}});
    }
    selectDirectMessage(userCode);
}

void GuiChatController::sendAdminAction(const QString& action, const QString& targetUserCode, const QString& messageId) {
    QMetaObject::invokeMethod(worker_, "sendAdminAction", Qt::QueuedConnection,
                              Q_ARG(QString, action), Q_ARG(QString, targetUserCode), Q_ARG(QString, messageId));
}

void GuiChatController::copyText(const QString& text) {
    if (QGuiApplication::clipboard()) QGuiApplication::clipboard()->setText(text);
}

void GuiChatController::removeLocalMessage(const QString& messageId) {
    if (messageId.isEmpty()) return;
    for (ChatListModel* model : conversationModels_) {
        model->removeRowsByValue("messageId", messageId);
    }
}

void GuiChatController::recallMessage(const QString& messageId) {
    if (!admin_ || messageId.isEmpty()) return;
    QMetaObject::invokeMethod(worker_, "sendAdminAction", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("recall")),
                              Q_ARG(QString, QString()), Q_ARG(QString, messageId));
}

void GuiChatController::handleConnected(bool isAdmin) {
    resetSessionData();
    connected_ = true;
    if (admin_ != isAdmin) {
        admin_ = isAdmin;
        emit adminChanged();
    }
    emit connectedChanged();
    refreshTimer_.start();
    setStatus(isAdmin ? QStringLiteral("已连接（管理员）") : QStringLiteral("已连接"));
    requestRooms();
    requestUsers();
}

void GuiChatController::resetSessionData() {
    roomModel_->clear();
    roomModel_->append({{"roomName", "lobby"}, {"memberCount", 0}, {"unreadCount", 0}});
    directMessageModel_->clear();
    memberModel_->clear();
    for (ChatListModel* model : conversationModels_) {
        model->clear();
    }
    activeConversationKey_ = QStringLiteral("room:lobby");
    joinedRoom_ = QStringLiteral("lobby");
    messageModel_ = conversationModels_.value(activeConversationKey_);
    onlineMemberCount_ = 0;
    emit onlineMemberCountChanged();
    emit activeMessageModelChanged();
}

void GuiChatController::handleConnectionFailed(const QString& reason) {
    refreshTimer_.stop();
    connected_ = false;
    if (admin_) {
        admin_ = false;
        emit adminChanged();
    }
    emit connectedChanged();
    setStatus(QStringLiteral("连接失败：") + reason);
    appendSystemMessage(statusText_);
}

void GuiChatController::handleConnectionLost(const QString& reason) {
    refreshTimer_.stop();
    connected_ = false;
    if (admin_) {
        admin_ = false;
        emit adminChanged();
    }
    emit connectedChanged();
    setStatus(reason.isEmpty() ? QStringLiteral("连接已断开") : QStringLiteral("连接已断开：") + reason);
    appendSystemMessage(statusText_);
}

void GuiChatController::handleMessage(const QString& type, const QString& messageId, const QString& username,
                                      const QString& userCode, const QString& content,
                                      const QString& room, const QString& targetUserCode,
                                      const QStringList& users, const QStringList& rooms, bool isAdmin) {
    if (type == QStringLiteral("login_ok")) {
        if (admin_ != isAdmin) {
            admin_ = isAdmin;
            emit adminChanged();
        }
        return;
    }

    if (type == QStringLiteral("users_response")) {
        QHash<QString, int> unreadByUser;
        for (int row = 0; row < directMessageModel_->rowCount(); ++row) {
            unreadByUser.insert(directMessageModel_->valueAt(row, "userCode").toString().toLower(),
                                directMessageModel_->valueAt(row, "unreadCount").toInt());
        }
        roomMemberCounts_.clear();
        memberModel_->clear();
        directMessageModel_->clear();
        for (const QString& identity : users) {
            const int hash = identity.indexOf('#');
            const int roomSeparator = identity.indexOf('@', hash + 1);
            const QString name = hash > 0 ? identity.left(hash) : identity;
            const QString code = hash > 0
                ? identity.mid(hash + 1, roomSeparator > hash ? roomSeparator - hash - 1 : -1)
                : QString();
            const QString userRoom = roomSeparator > hash ? identity.mid(roomSeparator + 1) : QStringLiteral("lobby");
            roomMemberCounts_[userRoom] = roomMemberCounts_.value(userRoom, 0) + 1;
            memberModel_->append({{"displayName", name}, {"userCode", code}, {"online", true}, {"admin", false}});
            if (!localUserCode_.isEmpty() &&
                code.compare(localUserCode_, Qt::CaseInsensitive) != 0) {
                const int unreadCount = unreadByUser.value(code.toLower(), 0);
                directMessageModel_->append({{"displayName", name},
                                             {"userCode", code},
                                             {"unreadCount", unreadCount}});
            }
        }
        for (int row = 0; row < roomModel_->rowCount(); ++row) {
            const QString roomName = roomModel_->valueAt(row, "roomName").toString();
            roomModel_->updateRow(row, {{"memberCount", roomMemberCounts_.value(roomName, 0)}});
        }
        if (onlineMemberCount_ != memberModel_->rowCount()) {
            onlineMemberCount_ = memberModel_->rowCount();
            emit onlineMemberCountChanged();
        }
        return;
    }

    if (type == QStringLiteral("rooms_response")) {
        QHash<QString, int> unreadByRoom;
        for (int row = 0; row < roomModel_->rowCount(); ++row) {
            unreadByRoom.insert(roomModel_->valueAt(row, "roomName").toString(),
                                roomModel_->valueAt(row, "unreadCount").toInt());
        }
        roomModel_->clear();
        for (const QString& room : rooms) {
            roomModel_->append({{"roomName", room}, {"memberCount", 0},
                                {"unreadCount", unreadByRoom.value(room, 0)}});
            const int row = roomModel_->rowCount() - 1;
            roomModel_->updateRow(row, {{"memberCount", roomMemberCounts_.value(room, 0)}});
        }
        return;
    }

    if (type == QStringLiteral("chat") || type == QStringLiteral("private_message") ||
        type == QStringLiteral("private_chat") || type == QStringLiteral("offline_message")) {
        QString key;
        if (type == QStringLiteral("private_message") || type == QStringLiteral("private_chat") ||
            type == QStringLiteral("offline_message")) {
            // 服务端给发送者和接收者都带 target_user_code。会话 key 必须使用
            // 对方代码：发送者使用 target，接收者使用 sender(userCode)。
            const bool isSelf = !localUserCode_.isEmpty() &&
                userCode.compare(localUserCode_, Qt::CaseInsensitive) == 0;
            const QString peerCode = isSelf
                ? targetUserCode
                : userCode;
            key = "dm:" + peerCode;
        } else {
            key = "room:" + (room.isEmpty() ? QStringLiteral("lobby") : room);
        }
        const bool isSelf = !localUserCode_.isEmpty() && userCode.compare(localUserCode_, Qt::CaseInsensitive) == 0;
        const QString effectiveUsername = username.trimmed().isEmpty()
            ? (isSelf ? localUserName_ : (userCode.trimmed().isEmpty() ? QStringLiteral("未知用户") : userCode))
            : username;
        const QString effectiveMessageId = messageId.isEmpty()
            ? QStringLiteral("local-%1").arg(++localMessageCounter_)
            : messageId;
        ensureConversationModel(key)->append({{"messageId", effectiveMessageId}, {"displayName", effectiveUsername}, {"userCode", userCode},
                                               {"time", QDateTime::currentDateTime().toString("HH:mm")},
                                               {"content", content}, {"selfMessage", isSelf}, {"systemMessage", false}});
        if (!isSelf && key != activeConversationKey_) {
            incrementUnreadForConversation(key, username, userCode);
        }
    } else if (type == QStringLiteral("message_recalled")) {
        if (!messageId.isEmpty()) {
            for (ChatListModel* model : conversationModels_) model->removeRowsByValue("messageId", messageId);
        }
    } else if (type == QStringLiteral("system") && !room.isEmpty()) {
        // 系统提示属于服务端广播时所在的房间，不能跟随当前打开的私聊窗口。
        appendSystemMessageToModel(ensureConversationModel("room:" + room), content);
    } else if (type == QStringLiteral("system") || type == QStringLiteral("error")) {
        appendSystemMessage(content);
    }

    Q_UNUSED(isAdmin);
}

void GuiChatController::setStatus(const QString& status) {
    if (statusText_ == status) return;
    statusText_ = status;
    emit statusTextChanged();
}

void GuiChatController::appendSystemMessage(const QString& content) {
    appendSystemMessageToModel(messageModel_, content);
}

void GuiChatController::appendSystemMessageToModel(ChatListModel* model, const QString& content) {
    if (!model) return;
    model->append({{"messageId", ""}, {"displayName", ""}, {"userCode", ""},
                   {"time", QDateTime::currentDateTime().toString("HH:mm")},
                   {"content", content}, {"selfMessage", false}, {"systemMessage", true}});
}

void GuiChatController::incrementUnreadForConversation(const QString& key,
                                                        const QString& username,
                                                        const QString& userCode) {
    if (key.startsWith("room:")) {
        const QString room = key.mid(5);
        const int row = roomModel_->findRow("roomName", room);
        if (row >= 0) {
            const int count = roomModel_->valueAt(row, "unreadCount").toInt();
            roomModel_->updateRow(row, {{"unreadCount", count + 1}});
        }
        return;
    }

    const QString code = key.mid(3);
    int row = -1;
    for (int i = 0; i < directMessageModel_->rowCount(); ++i) {
        if (directMessageModel_->valueAt(i, "userCode").toString()
                .compare(code, Qt::CaseInsensitive) == 0) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        directMessageModel_->append({{"displayName", username}, {"userCode", userCode},
                                     {"unreadCount", 1}});
        return;
    }
    const int count = directMessageModel_->valueAt(row, "unreadCount").toInt();
    directMessageModel_->updateRow(row, {{"unreadCount", count + 1}});
}

ChatListModel* GuiChatController::ensureConversationModel(const QString& key) {
    if (conversationModels_.contains(key)) return conversationModels_.value(key);
    auto* model = new ChatListModel(kMessageRoles, this);
    conversationModels_.insert(key, model);
    return model;
}
