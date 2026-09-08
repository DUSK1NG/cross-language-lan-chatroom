#include "gui_chat_controller.hpp"

#include "gui_connection_worker.hpp"
#include "host_path_resolver.hpp"

#include <QDateTime>
#include <QFileDialog>
#include <QGuiApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QSettings>
#include <QUuid>
#include <QVariantMap>
#include <QJsonDocument>
#include <QJsonObject>

#include <limits>

namespace {
const QStringList kMessageRoles = {"messageId", "displayName", "userCode", "time", "content", "selfMessage", "systemMessage", "deliveryState", "attachment"};
constexpr auto kConnectionGroup = "connection";
constexpr auto kServerIpKey = "serverIp";
constexpr auto kServerPortKey = "serverPort";
constexpr auto kUsernameKey = "username";
constexpr auto kUserCodeKey = "userCode";
constexpr auto kCaFileKey = "caFile";
constexpr int kMaxCachedConversationModels = 8;

QSettings connectionSettings() {
    return QSettings();
}
}

GuiChatController::GuiChatController(QObject* parent)
    : QObject(parent),
      roomModel_(new ChatListModel({"roomName", "memberCount", "unreadCount", "ownerCode", "private", "canManage"}, this)),
      directMessageModel_(new ChatListModel({"displayName", "userCode", "unreadCount"}, this)),
      messageModel_(new ChatListModel(kMessageRoles, this)),
      memberModel_(new ChatListModel({"displayName", "userCode", "online", "admin"}, this)),
      worker_(new GuiConnectionWorker) {
    conversationModels_.insert("room:lobby", messageModel_);
    conversationModelAccessOrder_.insert("room:lobby", ++conversationModelAccessSequence_);

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
    connect(worker_, &GuiConnectionWorker::reconnectScheduled, this, &GuiChatController::handleReconnectScheduled);
    connect(worker_, &GuiConnectionWorker::reconnectAttempt, this, &GuiChatController::handleReconnectAttempt);
    connect(worker_, &GuiConnectionWorker::reconnectFailed, this, &GuiChatController::handleReconnectFailed);
    connect(worker_, &GuiConnectionWorker::messageDeliveryFailed, this, [this](const QString& messageId) {
        for (ChatListModel* model : conversationModels_) {
            const int row = model->findRow("messageId", messageId);
            if (row >= 0) model->updateRow(row, {{"deliveryState", "failed"}});
        }
    });
    connect(worker_, &GuiConnectionWorker::messageReceived, this, &GuiChatController::handleMessage);
    connect(worker_, &GuiConnectionWorker::historyReceived, this, &GuiChatController::handleHistory);
    connect(worker_, &GuiConnectionWorker::mlsCommandResult, this, &GuiChatController::mlsCommandResult);
    connect(worker_, &GuiConnectionWorker::mlsKeyPackageAvailable, this, &GuiChatController::mlsKeyPackageAvailable);
    connect(worker_, &GuiConnectionWorker::mlsWelcomeEvent, this, &GuiChatController::mlsWelcomeEvent);
    connect(worker_, &GuiConnectionWorker::mlsGroupState, this, &GuiChatController::mlsGroupState);
    connect(worker_, &GuiConnectionWorker::mlsDataResult, this, &GuiChatController::mlsDataResult);
    connect(worker_, &GuiConnectionWorker::attachmentEvent, this, &GuiChatController::attachmentEvent);
    connect(worker_, &GuiConnectionWorker::attachmentEvent, this, &GuiChatController::handleAttachmentEvent);
    workerThread_.start();
}

GuiChatController::~GuiChatController() {
    refreshTimer_.stop();
    // The controller is being destroyed, so the worker must finish its
    // socket/process cleanup before the thread is joined. The interactive
    // disconnect path below remains asynchronous and never waits on the GUI.
    if (worker_ && workerThread_.isRunning()) {
        QMetaObject::invokeMethod(worker_, "disconnectFromServer", Qt::BlockingQueuedConnection);
    }
    workerThread_.quit();
    workerThread_.wait();
}

void GuiChatController::connectToServer(const QString& serverIp, int serverPort,
                                        const QString& username, const QString& userCode,
                                        const QString& caFile) {
    connectToServerWithTlsName(serverIp, serverPort, username, userCode, caFile, {});
}

#if defined(LAN_CHAT_ENABLE_MLSPP) && defined(LAN_CHAT_ENABLE_TEST_HOOKS)
void GuiChatController::enableDropNextMlsCommitForTesting() {
    QMetaObject::invokeMethod(worker_, "enableDropNextMlsCommitForTesting", Qt::QueuedConnection);
}
#endif

void GuiChatController::connectToServerWithTlsName(const QString& serverIp, int serverPort,
                                                   const QString& username, const QString& userCode,
                                                   const QString& caFile, const QString& tlsServerName) {
    saveConnectionPreferences(serverIp, serverPort, username, userCode, caFile);
    const bool identityChanged = localUserName_ != username || localUserCode_ != userCode;
    localUserName_ = username;
    localUserCode_ = userCode;
    if (identityChanged) emit localIdentityChanged();
    setStatus(QStringLiteral("正在连接..."));
    QMetaObject::invokeMethod(worker_, "connectToServer", Qt::QueuedConnection,
                              Q_ARG(QString, serverIp), Q_ARG(int, serverPort),
                              Q_ARG(QString, username), Q_ARG(QString, userCode),
                              Q_ARG(QString, caFile), Q_ARG(QString, tlsServerName));
}

void GuiChatController::connectToLocalHost(const QString& serverExe,
                                            const QString& certFile,
                                            const QString& keyFile,
                                            const QString& dbFile,
                                            const QString& username,
                                            const QString& userCode) {
    saveConnectionPreferences(QStringLiteral("127.0.0.1"), 8888, username, userCode, certFile);
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

QString GuiChatController::savedServerIp() const {
    QSettings settings = connectionSettings();
    settings.beginGroup(QLatin1String(kConnectionGroup));
    const QString value = settings.value(QLatin1String(kServerIpKey), QStringLiteral("127.0.0.1")).toString().trimmed();
    settings.endGroup();
    return value.isEmpty() ? QStringLiteral("127.0.0.1") : value;
}

int GuiChatController::savedServerPort() const {
    QSettings settings = connectionSettings();
    settings.beginGroup(QLatin1String(kConnectionGroup));
    const int value = settings.value(QLatin1String(kServerPortKey), 8888).toInt();
    settings.endGroup();
    return value >= 1 && value <= 65535 ? value : 8888;
}

QString GuiChatController::savedUsername() const {
    QSettings settings = connectionSettings();
    settings.beginGroup(QLatin1String(kConnectionGroup));
    const QString value = settings.value(QLatin1String(kUsernameKey)).toString().trimmed();
    settings.endGroup();
    return value;
}

QString GuiChatController::savedUserCode() const {
    QSettings settings = connectionSettings();
    settings.beginGroup(QLatin1String(kConnectionGroup));
    const QString value = settings.value(QLatin1String(kUserCodeKey)).toString().trimmed();
    settings.endGroup();
    return value;
}

QString GuiChatController::savedCaFile() const {
    QSettings settings = connectionSettings();
    settings.beginGroup(QLatin1String(kConnectionGroup));
    const QString savedPath = settings.value(QLatin1String(kCaFileKey)).toString().trimmed();
    settings.endGroup();
    if (!savedPath.isEmpty() && QFileInfo::exists(savedPath)) {
        return QFileInfo(savedPath).absoluteFilePath();
    }
    if (!bundledCaFile_.isEmpty() && QFileInfo::exists(bundledCaFile_)) {
        return QFileInfo(bundledCaFile_).absoluteFilePath();
    }
    return {};
}

void GuiChatController::setBundledCaFile(const QString& path) {
    bundledCaFile_ = QFileInfo(path).absoluteFilePath();
    emit savedConnectionChanged();
}

void GuiChatController::saveConnectionPreferences(const QString& serverIp, int serverPort,
                                                   const QString& username, const QString& userCode,
                                                   const QString& caFile) {
    QSettings settings = connectionSettings();
    settings.beginGroup(QLatin1String(kConnectionGroup));
    settings.setValue(QLatin1String(kServerIpKey), serverIp.trimmed());
    settings.setValue(QLatin1String(kServerPortKey), serverPort);
    settings.setValue(QLatin1String(kUsernameKey), username.trimmed());
    settings.setValue(QLatin1String(kUserCodeKey), userCode.trimmed());
    if (caFile.trimmed().isEmpty()) {
        settings.remove(QLatin1String(kCaFileKey));
    } else if (QFileInfo::exists(caFile)) {
        settings.setValue(QLatin1String(kCaFileKey), QFileInfo(caFile).absoluteFilePath());
    }
    settings.endGroup();
    settings.sync();
    emit savedConnectionChanged();
}

void GuiChatController::disconnectFromServer() {
    refreshTimer_.stop();
    if (worker_) {
        QMetaObject::invokeMethod(worker_, "disconnectFromServer", Qt::QueuedConnection);
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
    const QString trimmedContent = content.trimmed();
    const QString normalizedRoom = room.trimmed().isEmpty() ? QStringLiteral("lobby") : room.trimmed();
    if (trimmedContent.isEmpty()) return;
    const QString messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ensureConversationModel("room:" + normalizedRoom)->append({
        {"messageId", messageId}, {"displayName", localUserName_}, {"userCode", localUserCode_},
        {"time", QDateTime::currentDateTime().toString("HH:mm")}, {"content", trimmedContent},
        {"selfMessage", true}, {"systemMessage", false}, {"deliveryState", "queued"}});
    QMetaObject::invokeMethod(worker_, "sendChatToRoom", Qt::QueuedConnection,
                              Q_ARG(QString, trimmedContent), Q_ARG(QString, normalizedRoom), Q_ARG(QString, messageId));
}

void GuiChatController::sendPrivateMessage(const QString& content, const QString& targetUserCode) {
    const QString trimmedContent = content.trimmed();
    const QString normalizedTarget = targetUserCode.trimmed();
    if (trimmedContent.isEmpty() || normalizedTarget.isEmpty()) return;
    const QString messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ensureConversationModel("dm:" + normalizedTarget)->append({
        {"messageId", messageId}, {"displayName", localUserName_}, {"userCode", localUserCode_},
        {"time", QDateTime::currentDateTime().toString("HH:mm")}, {"content", trimmedContent},
        {"selfMessage", true}, {"systemMessage", false}, {"deliveryState", "queued"}});
    QMetaObject::invokeMethod(worker_, "sendPrivate", Qt::QueuedConnection,
                              Q_ARG(QString, trimmedContent), Q_ARG(QString, normalizedTarget), Q_ARG(QString, messageId));
}

void GuiChatController::requestUsers() {
    QMetaObject::invokeMethod(worker_, "requestUsers", Qt::QueuedConnection);
}

void GuiChatController::requestRooms() {
    QMetaObject::invokeMethod(worker_, "requestRooms", Qt::QueuedConnection);
}

void GuiChatController::requestActiveHistory(const QString& beforeMessageId, const QString& searchQuery) {
    if (!connected_ || historyLoading_) return;
    const bool isPrivate = activeConversationKey_.startsWith("dm:");
    const QString room = isPrivate ? QString() : activeConversationKey_.mid(5);
    const QString peer = isPrivate ? activeConversationKey_.mid(3) : QString();
    historyLoading_ = true;
    QMetaObject::invokeMethod(worker_, "requestHistory", Qt::QueuedConnection,
                              Q_ARG(QString, room), Q_ARG(QString, peer), Q_ARG(bool, isPrivate),
                              Q_ARG(QString, beforeMessageId), Q_ARG(int, 50), Q_ARG(QString, searchQuery));
}

void GuiChatController::loadMoreHistory() {
    if (!connected_ || historyLoading_ || !historyHasMore_ || !historySearchQuery_.isEmpty() || messageModel_->rowCount() == 0) return;
    const QString before = messageModel_->valueAt(0, "messageId").toString();
    if (!before.isEmpty()) requestActiveHistory(before);
}

void GuiChatController::searchActiveHistory(const QString& query) {
    historySearchQuery_ = query.trimmed();
    replaceHistoryOnNextResponse_ = true;
    if (!historyLoading_) requestActiveHistory({}, historySearchQuery_);
}

void GuiChatController::createRoom(const QString& room, bool isPrivate) {
    if (room.trimmed().isEmpty()) {
        return;
    }
    QMetaObject::invokeMethod(worker_, "createRoom", Qt::QueuedConnection,
                              Q_ARG(QString, room.trimmed()), Q_ARG(bool, isPrivate));
}

void GuiChatController::sendRoomAction(const QString& action, const QString& room, const QString& targetUserCode) {
    QMetaObject::invokeMethod(worker_, "sendRoomAction", Qt::QueuedConnection,
                              Q_ARG(QString, action), Q_ARG(QString, room), Q_ARG(QString, targetUserCode));
}

void GuiChatController::selectRoom(const QString& room) {
    historySearchQuery_.clear();
    replaceHistoryOnNextResponse_ = false;
    const QString key = "room:" + room;
    messageModel_ = ensureConversationModel(key);
    activeConversationKey_ = key;
    const int row = roomModel_->findRow("roomName", room);
    const bool canManage = row >= 0 && roomModel_->valueAt(row, "canManage").toBool();
    if (row >= 0) roomModel_->updateRow(row, {{"unreadCount", 0}});
    if (activeRoomCanManage_ != canManage) {
        activeRoomCanManage_ = canManage;
        emit activeRoomCanManageChanged();
    }
    emit activeMessageModelChanged();
    if (connected_) {
        if (joinedRoom_.compare(room, Qt::CaseInsensitive) != 0) {
            QMetaObject::invokeMethod(worker_, "joinRoom", Qt::QueuedConnection, Q_ARG(QString, room));
            joinedRoom_ = room;
        }
        requestActiveHistory();
    }
    if (reconnecting_) {
        reconnecting_ = false;
        emit reconnectingChanged();
    }
}

void GuiChatController::selectDirectMessage(const QString& userCode) {
    historySearchQuery_.clear();
    replaceHistoryOnNextResponse_ = false;
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
    if (connected_) requestActiveHistory();
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

bool GuiChatController::recallMessage(const QString& messageId, const QString& commandId) {
    if (!canRecallMessage(messageId)) return false;
    QMetaObject::invokeMethod(worker_, "sendAdminAction", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("recall")),
                              Q_ARG(QString, QString()), Q_ARG(QString, messageId), Q_ARG(QString, commandId));
    return true;
}

void GuiChatController::handleConnected(bool isAdmin) {
    const bool recovered = reconnecting_;
    if (!recovered) resetSessionData();
    if (reconnecting_) {
        reconnecting_ = false;
        emit reconnectingChanged();
    }
    connected_ = true;
    if (admin_ != isAdmin) {
        admin_ = isAdmin;
        emit adminChanged();
    }
    emit connectedChanged();
    refreshTimer_.start();
    setStatus(recovered ? QStringLiteral("已重新连接")
                        : (isAdmin ? QStringLiteral("已连接（管理员）") : QStringLiteral("已连接")));
    requestRooms();
    requestUsers();
    requestActiveHistory();
}

void GuiChatController::resetSessionData() {
    roomModel_->replaceRows({{{"roomName", "lobby"}, {"memberCount", 0}, {"unreadCount", 0}}});
    directMessageModel_->replaceRows({});
    memberModel_->replaceRows({});
    for (ChatListModel* model : conversationModels_) {
        model->clear();
    }
    activeConversationKey_ = QStringLiteral("room:lobby");
    joinedRoom_ = QStringLiteral("lobby");
    messageModel_ = ensureConversationModel(activeConversationKey_);
    historyHasMore_ = false;
    historyLoading_ = false;
    historySearchQuery_.clear();
    replaceHistoryOnNextResponse_ = false;
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
    emit connectionFailed(reason);
}

void GuiChatController::handleConnectionLost(const QString& reason) {
    refreshTimer_.stop();
    connected_ = false;
    emit connectedChanged();
    if (!reconnecting_) {
        reconnecting_ = true;
        emit reconnectingChanged();
    }
    setStatus(reason.isEmpty() ? QStringLiteral("连接中断，正在重新连接") : QStringLiteral("连接中断，正在重新连接：") + reason);
    QMetaObject::invokeMethod(worker_, "scheduleReconnect", Qt::QueuedConnection);
    emit connectionLost(reason);
}

void GuiChatController::handleReconnectScheduled(int attempt, int delayMs) {
    Q_UNUSED(delayMs);
    setStatus(QStringLiteral("正在重新连接（第 %1 次）").arg(attempt));
}

void GuiChatController::handleReconnectAttempt(int attempt) {
    setStatus(QStringLiteral("正在重新连接（第 %1 次）").arg(attempt));
}

void GuiChatController::handleReconnectFailed(const QString& reason) {
    setStatus(QStringLiteral("重新连接失败，等待下一次重试：") + reason);
}

void GuiChatController::handleHistory(const QString& room, const QString& targetUserCode,
                                      bool isPrivate, const QVariantList& messages, bool hasMore,
                                      const QString& searchQuery) {
    historyLoading_ = false;
    const QString expectedKey = isPrivate ? "dm:" + targetUserCode : "room:" + room;
    if (expectedKey.compare(activeConversationKey_, Qt::CaseInsensitive) != 0) return;
    if (searchQuery != historySearchQuery_) {
        requestActiveHistory({}, historySearchQuery_);
        return;
    }

    QList<QVariantMap> rows;
    rows.reserve(messages.size());
    for (const QVariant& value : messages) {
        const QVariantMap detail = value.toMap();
        const QString messageId = detail.value("messageId").toString();
        if (messageId.isEmpty() || messageModel_->findRow("messageId", messageId) >= 0) continue;
        const QString createdAt = detail.value("createdAt").toString();
        const QDateTime timestamp = QDateTime::fromString(createdAt, Qt::ISODate);
        const QString time = timestamp.isValid()
            ? timestamp.toLocalTime().toString("HH:mm")
            : QDateTime::currentDateTime().toString("HH:mm");
        const QString userCode = detail.value("userCode").toString();
        rows.append({{"messageId", messageId},
                     {"displayName", detail.value("displayName")},
                     {"userCode", userCode},
                     {"time", time},
                     {"content", detail.value("content")},
                     {"selfMessage", userCode.compare(localUserCode_, Qt::CaseInsensitive) == 0},
                     {"systemMessage", false}, {"deliveryState", detail.value("deliveryState", "sent")}});
    }
    if (!rows.isEmpty()) messageModel_->prependRows(rows);
    historyHasMore_ = searchQuery.isEmpty() ? hasMore : false;
    replaceHistoryOnNextResponse_ = false;
}

void GuiChatController::handleMessage(const QString& type, const QString& messageId, const QString& commandId,
                                      const QString& username, const QString& userCode, const QString& content,
                                      const QString& room, const QString& targetUserCode, const QString& deliveryState,
                                      const QStringList& users, const QStringList& rooms,
                                      const QVariantList& userDetails, const QVariantList& roomDetails,
                                      bool isAdmin) {
    if (type == QStringLiteral("delivery_receipt")) {
        const QString state = deliveryState.isEmpty() ? content : deliveryState;
        if (messageId.isEmpty() || (state != QStringLiteral("sent") && state != QStringLiteral("delivered"))) return;
        for (ChatListModel* model : conversationModels_) {
            const int row = model->findRow("messageId", messageId);
            if (row >= 0) model->updateRow(row, {{"deliveryState", state}});
        }
        return;
    }
    if (type == QStringLiteral("login_ok")) {
        if (admin_ != isAdmin) {
            admin_ = isAdmin;
            emit adminChanged();
        }
        return;
    }

    if (type == QStringLiteral("connection_approval_request")) {
        if (messageId.isEmpty() || username.isEmpty() || userCode.isEmpty()) {
            return;
        }
        QVariantMap request{{"id", messageId}, {"displayName", username}, {"userCode", userCode},
                            {"requestedAt", content}};
        bool replaced = false;
        for (QVariant& value : pendingConnectionApprovals_) {
            if (value.toMap().value("id").toString() == messageId) {
                value = request;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            pendingConnectionApprovals_.append(request);
        }
        emit pendingConnectionApprovalsChanged();
        return;
    }

    if (type == QStringLiteral("connection_approval_result")) {
        bool removed = false;
        for (int index = pendingConnectionApprovals_.size() - 1; index >= 0; --index) {
            if (pendingConnectionApprovals_.at(index).toMap().value("id").toString() == messageId) {
                pendingConnectionApprovals_.removeAt(index);
                removed = true;
            }
        }
        if (removed) {
            emit pendingConnectionApprovalsChanged();
        }
        if (content == QStringLiteral("approved")) {
            appendSystemMessage(QStringLiteral("已批准成员连接，成员正在加入聊天室。"));
        } else if (content == QStringLiteral("denied")) {
            appendSystemMessage(QStringLiteral("已拒绝成员连接请求。"));
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
        QList<QVariantMap> memberRows;
        QList<QVariantMap> directMessageRows;
        const auto appendUser = [this, &unreadByUser, &memberRows, &directMessageRows](
                                    const QString& name, const QString& code,
                                    const QString& userRoom, bool memberAdmin) {
            roomMemberCounts_[userRoom] = roomMemberCounts_.value(userRoom, 0) + 1;
            memberRows.append({{"displayName", name}, {"userCode", code}, {"online", true}, {"admin", memberAdmin}});
            if (!localUserCode_.isEmpty() &&
                code.compare(localUserCode_, Qt::CaseInsensitive) != 0) {
                const int unreadCount = unreadByUser.value(code.toLower(), 0);
                directMessageRows.append({{"displayName", name},
                                          {"userCode", code},
                                          {"unreadCount", unreadCount}});
            }
        };
        if (!userDetails.isEmpty()) {
            for (const QVariant& value : userDetails) {
                const QVariantMap detail = value.toMap();
                appendUser(detail.value("displayName").toString(),
                           detail.value("userCode").toString(),
                           detail.value("room", QStringLiteral("lobby")).toString(),
                           detail.value("admin").toBool());
            }
        } else {
            for (const QString& identity : users) {
                const int hash = identity.indexOf('#');
                const int roomSeparator = identity.indexOf('@', hash + 1);
                const QString name = hash > 0 ? identity.left(hash) : identity;
                const QString code = hash > 0
                    ? identity.mid(hash + 1, roomSeparator > hash ? roomSeparator - hash - 1 : -1)
                    : QString();
                const QString userRoom = roomSeparator > hash ? identity.mid(roomSeparator + 1) : QStringLiteral("lobby");
                appendUser(name, code, userRoom, false);
            }
        }
        memberModel_->replaceRows(memberRows);
        directMessageModel_->replaceRows(directMessageRows);

        QList<QVariantMap> roomMemberUpdates;
        for (int row = 0; row < roomModel_->rowCount(); ++row) {
            const QString roomName = roomModel_->valueAt(row, "roomName").toString();
            roomMemberUpdates.append({{"memberCount", roomMemberCounts_.value(roomName, 0)}});
        }
        roomModel_->updateRows(roomMemberUpdates);
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
        QList<QVariantMap> roomRows;
        const auto appendRoom = [this, &unreadByRoom, &roomRows](
                                    const QString& roomName, const QString& ownerCode,
                                    bool isPrivate, bool canManage) {
            roomRows.append({{"roomName", roomName},
                             {"memberCount", roomMemberCounts_.value(roomName, 0)},
                             {"unreadCount", unreadByRoom.value(roomName, 0)},
                             {"ownerCode", ownerCode}, {"private", isPrivate},
                             {"canManage", canManage}});
        };
        if (!roomDetails.isEmpty()) {
            for (const QVariant& value : roomDetails) {
                const QVariantMap detail = value.toMap();
                appendRoom(detail.value("roomName").toString(), detail.value("ownerCode").toString(),
                           detail.value("private").toBool(), detail.value("canManage").toBool());
            }
        } else {
            for (const QString& room : rooms) {
                appendRoom(room, {}, false, admin_);
            }
        }
        roomModel_->replaceRows(roomRows);
        const QString activeRoom = activeConversationKey_.startsWith("room:") ? activeConversationKey_.mid(5) : QString();
        const int activeRow = roomModel_->findRow("roomName", activeRoom);
        const bool canManage = activeRow >= 0 && roomModel_->valueAt(activeRow, "canManage").toBool();
        if (activeRoomCanManage_ != canManage) {
            activeRoomCanManage_ = canManage;
            emit activeRoomCanManageChanged();
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
        QVariantMap messageRow{{"messageId", effectiveMessageId}, {"displayName", effectiveUsername}, {"userCode", userCode},
                               {"time", QDateTime::currentDateTime().toString("HH:mm")},
                               {"content", content}, {"selfMessage", isSelf}, {"systemMessage", false},
                               {"deliveryState", deliveryState.isEmpty() ? QStringLiteral("sent") : deliveryState}};
        if (pendingAttachmentMetadata_.contains(effectiveMessageId)) {
            messageRow.insert(QStringLiteral("attachment"), pendingAttachmentMetadata_.take(effectiveMessageId));
        }
        ensureConversationModel(key)->append(messageRow);
        if (!isSelf && key != activeConversationKey_) {
            incrementUnreadForConversation(key, username, userCode);
        }
    } else if (type == QStringLiteral("message_recalled")) {
        if (!messageId.isEmpty()) {
            for (ChatListModel* model : conversationModels_) model->removeRowsByValue("messageId", messageId);
			emit recallSucceeded(commandId);
        }
    } else if (type == QStringLiteral("system") && !room.isEmpty()) {
        // 系统提示属于服务端广播时所在的房间，不能跟随当前打开的私聊窗口。
        appendSystemMessageToModel(ensureConversationModel("room:" + room), content);
    } else if (type == QStringLiteral("error")) {
        bool associatedWithMessage = false;
        if (!messageId.isEmpty()) {
            for (ChatListModel* model : conversationModels_) {
                const int row = model->findRow("messageId", messageId);
                if (row >= 0) {
                    model->updateRow(row, {{"deliveryState", "failed"}});
                    associatedWithMessage = true;
                }
            }
        }
        if (!commandId.isEmpty()) emit recallFailed(commandId, content);
        // Attachment, protocol and rate-limit errors have an operation or
        // message identity. Their dedicated bridge/UI paths display them; do
        // not persist a transient transport failure into the chat timeline.
        if (!associatedWithMessage && commandId.isEmpty()) setStatus(content);
    } else if (type == QStringLiteral("system")) {
        appendSystemMessage(content);
    }

    Q_UNUSED(isAdmin);
}

bool GuiChatController::canRecallMessage(const QString& messageId) const {
    if (messageId.isEmpty()) return false;
    if (admin_) return true;
    for (ChatListModel* model : conversationModels_) {
        const int row = model->findRow("messageId", messageId);
        if (row >= 0 && model->valueAt(row, "userCode").toString().compare(localUserCode_, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
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
                   {"content", content}, {"selfMessage", false}, {"systemMessage", true}, {"deliveryState", ""}});
}

bool GuiChatController::retryMessage(const QString& messageId) {
    if (messageId.isEmpty()) return false;
    for (auto it = conversationModels_.cbegin(); it != conversationModels_.cend(); ++it) {
        ChatListModel* model = it.value();
        const int row = model->findRow("messageId", messageId);
        if (row < 0 || !model->valueAt(row, "selfMessage").toBool() ||
            model->valueAt(row, "deliveryState").toString() != QStringLiteral("failed")) continue;
        const QString content = model->valueAt(row, "content").toString();
        model->updateRow(row, {{"deliveryState", "queued"}});
        if (it.key().startsWith("dm:")) {
            QMetaObject::invokeMethod(worker_, "sendPrivate", Qt::QueuedConnection,
                                      Q_ARG(QString, content), Q_ARG(QString, it.key().mid(3)), Q_ARG(QString, messageId));
        } else {
            QMetaObject::invokeMethod(worker_, "sendChatToRoom", Qt::QueuedConnection,
                                      Q_ARG(QString, content), Q_ARG(QString, it.key().mid(5)), Q_ARG(QString, messageId));
        }
        return true;
    }
    return false;
}

void GuiChatController::handleAttachmentEvent(const QString& type, const QString& attachmentId,
                                               const QString&, const QString& commandId,
                                               qint64, qint64, const QByteArray&, const QByteArray&,
                                               const QString&, const QList<qint64>&, const QString& content) {
    if (type != QStringLiteral("manifest") || attachmentId.isEmpty() || commandId.isEmpty()) return;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(content.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return;
    const QJsonObject metadata = document.object();
    const QVariantMap attachment{
        {QStringLiteral("attachmentId"), attachmentId},
        {QStringLiteral("fileName"), metadata.value(QStringLiteral("fileName")).toString()},
        {QStringLiteral("logicalSize"), metadata.value(QStringLiteral("logicalSize")).toVariant()},
        {QStringLiteral("status"), QStringLiteral("available")},
        {QStringLiteral("totalChunks"), metadata.value(QStringLiteral("totalChunks")).toVariant()}
    };
    for (ChatListModel* model : conversationModels_) {
        const int row = model->findRow("messageId", commandId);
        if (row >= 0) {
            model->updateRow(row, {{QStringLiteral("attachment"), attachment}});
            return;
        }
    }
    pendingAttachmentMetadata_.insert(commandId, attachment);
}

void GuiChatController::sendAttachmentInit(const QString& room, const qint64 logicalSize,
                                            const QString& commandId) {
    QMetaObject::invokeMethod(worker_, "sendAttachmentInit", Qt::QueuedConnection,
                              Q_ARG(QString, room), Q_ARG(qint64, logicalSize), Q_ARG(QString, commandId));
}

void GuiChatController::startAttachmentUpload(const QString& room, const QString& filePath,
                                              const QStringList& targetUsers, const QString& commandId) {
    QMetaObject::invokeMethod(worker_, "startAttachmentUpload", Qt::QueuedConnection,
                              Q_ARG(QString, room), Q_ARG(QString, filePath),
                              Q_ARG(QStringList, targetUsers), Q_ARG(QString, commandId));
}

void GuiChatController::chooseAndUploadAttachment(const QString& room, const QString& commandId) {
    const QString filePath = QFileDialog::getOpenFileName(nullptr, QStringLiteral("选择附件"));
    if (filePath.isEmpty()) return;
    QStringList targetUsers;
    for (int row = 0; row < memberModel_->rowCount(); ++row) {
        if (!memberModel_->valueAt(row, QByteArrayLiteral("online")).toBool()) continue;
        const QString userCode = memberModel_->valueAt(row, QByteArrayLiteral("userCode")).toString();
        if (!userCode.isEmpty() && userCode.compare(localUserCode_, Qt::CaseInsensitive) != 0) {
            targetUsers.append(userCode);
        }
    }
    startAttachmentUpload(room, filePath, targetUsers, commandId);
}

void GuiChatController::sendAttachmentChunk(const QString& uploadId, const qint64 chunkIndex,
                                             const QByteArray& ciphertext, const QByteArray& cipherSha256,
                                             const QString& commandId) {
    QMetaObject::invokeMethod(worker_, "sendAttachmentChunk", Qt::QueuedConnection,
                              Q_ARG(QString, uploadId), Q_ARG(qint64, chunkIndex),
                              Q_ARG(QByteArray, ciphertext), Q_ARG(QByteArray, cipherSha256),
                              Q_ARG(QString, commandId));
}

void GuiChatController::resumeAttachment(const QString& uploadId, const QString& commandId) {
    QMetaObject::invokeMethod(worker_, "resumeAttachment", Qt::QueuedConnection,
                              Q_ARG(QString, uploadId), Q_ARG(QString, commandId));
}

void GuiChatController::sendAttachmentCommit(const QString& uploadId, const QString& commandId) {
    QMetaObject::invokeMethod(worker_, "sendAttachmentCommit", Qt::QueuedConnection,
                              Q_ARG(QString, uploadId), Q_ARG(QString, commandId));
}

void GuiChatController::sendAttachmentDownload(const QString& attachmentId, const qint64 chunkIndex,
                                                const QString& commandId) {
    Q_UNUSED(chunkIndex);
    const QString outputPath = QFileDialog::getSaveFileName(nullptr, QStringLiteral("保存附件"));
    if (outputPath.isEmpty()) return;
    startAttachmentDownload(attachmentId, outputPath, commandId);
}

void GuiChatController::startAttachmentDownload(const QString& attachmentId, const QString& outputPath,
                                                const QString& commandId) {
    QMetaObject::invokeMethod(worker_, "startAttachmentDownload", Qt::QueuedConnection,
                              Q_ARG(QString, attachmentId), Q_ARG(QString, outputPath),
                              Q_ARG(QString, commandId));
}

void GuiChatController::fetchMlsKeyPackage(const QString& room, const QString& targetUserCode,
                                           const QString& commandId) {
    QMetaObject::invokeMethod(worker_, "fetchMlsKeyPackage", Qt::QueuedConnection,
                              Q_ARG(QString, room), Q_ARG(QString, targetUserCode),
                              Q_ARG(QString, commandId));
}

void GuiChatController::addMlsMember(const QString& room, const QString& groupId,
                                     const QString& targetUserCode, const QString& commandId) {
    QMetaObject::invokeMethod(worker_, "addMlsMember", Qt::QueuedConnection,
                              Q_ARG(QString, room), Q_ARG(QString, groupId),
                              Q_ARG(QString, targetUserCode), Q_ARG(QString, commandId));
}

void GuiChatController::removeMlsMember(const QString& room, const QString& groupId,
                                         const QString& targetUserCode, const QString& commandId) {
    QMetaObject::invokeMethod(worker_, "removeMlsMember", Qt::QueuedConnection,
                              Q_ARG(QString, room), Q_ARG(QString, groupId),
                              Q_ARG(QString, targetUserCode), Q_ARG(QString, commandId));
}

void GuiChatController::inspectMlsGroup(const QString& groupId, const QString& commandId) {
    QMetaObject::invokeMethod(worker_, "inspectMlsGroup", Qt::QueuedConnection,
                              Q_ARG(QString, groupId), Q_ARG(QString, commandId));
}

void GuiChatController::protectMls(const QString& groupId, const QByteArray& plaintext,
                                   const QString& commandId) {
    QMetaObject::invokeMethod(worker_, "protectMls", Qt::QueuedConnection,
                              Q_ARG(QString, groupId), Q_ARG(QByteArray, plaintext), Q_ARG(QString, commandId));
}

void GuiChatController::unprotectMls(const QString& groupId, const QByteArray& ciphertext,
                                     const QString& commandId) {
    QMetaObject::invokeMethod(worker_, "unprotectMls", Qt::QueuedConnection,
                              Q_ARG(QString, groupId), Q_ARG(QByteArray, ciphertext), Q_ARG(QString, commandId));
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
    if (conversationModels_.contains(key)) {
        conversationModelAccessOrder_.insert(key, ++conversationModelAccessSequence_);
        return conversationModels_.value(key);
    }
    auto* model = new ChatListModel(kMessageRoles, this);
    conversationModels_.insert(key, model);
    conversationModelAccessOrder_.insert(key, ++conversationModelAccessSequence_);
    trimConversationModelCache(key);
    return model;
}

void GuiChatController::trimConversationModelCache(const QString& protectedKey) {
    while (conversationModels_.size() > kMaxCachedConversationModels) {
        QString leastRecentlyUsedKey;
        quint64 leastRecentlyUsedOrder = std::numeric_limits<quint64>::max();
        for (auto it = conversationModels_.cbegin(); it != conversationModels_.cend(); ++it) {
            if (it.key() == activeConversationKey_ || it.key() == protectedKey) continue;
            const quint64 order = conversationModelAccessOrder_.value(it.key());
            if (order < leastRecentlyUsedOrder) {
                leastRecentlyUsedOrder = order;
                leastRecentlyUsedKey = it.key();
            }
        }
        if (leastRecentlyUsedKey.isEmpty()) return;
        delete conversationModels_.take(leastRecentlyUsedKey);
        conversationModelAccessOrder_.remove(leastRecentlyUsedKey);
    }
}
