#pragma once

#include "chat_model.hpp"
#include "conversation_filter_model.hpp"

#include <QAbstractItemModel>
#include <QHash>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVariantList>

class GuiConnectionWorker;

class GuiChatController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(ChatListModel* roomModel READ roomModel CONSTANT)
    Q_PROPERTY(ChatListModel* directMessageModel READ directMessageModel CONSTANT)
    Q_PROPERTY(ChatListModel* messageModel READ messageModel CONSTANT)
    Q_PROPERTY(ChatListModel* activeMessageModel READ activeMessageModel NOTIFY activeMessageModelChanged)
    Q_PROPERTY(ChatListModel* memberModel READ memberModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* filteredRoomModel READ filteredRoomModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* filteredDirectMessageModel READ filteredDirectMessageModel CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool reconnecting READ reconnecting NOTIFY reconnectingChanged)
    Q_PROPERTY(bool admin READ admin NOTIFY adminChanged)
    Q_PROPERTY(QString localUserName READ localUserName NOTIFY localIdentityChanged)
    Q_PROPERTY(QString localUserCode READ localUserCode NOTIFY localIdentityChanged)
    Q_PROPERTY(int onlineMemberCount READ onlineMemberCount NOTIFY onlineMemberCountChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool activeRoomCanManage READ activeRoomCanManage NOTIFY activeRoomCanManageChanged)
    Q_PROPERTY(QString savedServerIp READ savedServerIp CONSTANT)
    Q_PROPERTY(int savedServerPort READ savedServerPort CONSTANT)
    Q_PROPERTY(QString savedUsername READ savedUsername CONSTANT)
    Q_PROPERTY(QString savedUserCode READ savedUserCode CONSTANT)
    Q_PROPERTY(QString savedCaFile READ savedCaFile NOTIFY savedConnectionChanged)

public:
    explicit GuiChatController(QObject* parent = nullptr);
    ~GuiChatController() override;

    ChatListModel* roomModel() const { return roomModel_; }
    ChatListModel* directMessageModel() const { return directMessageModel_; }
    ChatListModel* messageModel() const { return messageModel_; }
    ChatListModel* activeMessageModel() const { return messageModel_; }
    ChatListModel* memberModel() const { return memberModel_; }
    QAbstractItemModel* filteredRoomModel() const { return roomFilterModel_; }
    QAbstractItemModel* filteredDirectMessageModel() const { return directMessageFilterModel_; }
    bool connected() const { return connected_; }
    bool reconnecting() const { return reconnecting_; }
    bool admin() const { return admin_; }
    QString localUserName() const { return localUserName_; }
    QString localUserCode() const { return localUserCode_; }
    int onlineMemberCount() const { return onlineMemberCount_; }
    QString statusText() const { return statusText_; }
    bool activeRoomCanManage() const { return activeRoomCanManage_; }
    QString activeConversationKey() const { return activeConversationKey_; }
    QString savedServerIp() const;
    int savedServerPort() const;
    QString savedUsername() const;
    QString savedUserCode() const;
    QString savedCaFile() const;
    QVariantList pendingConnectionApprovals() const { return pendingConnectionApprovals_; }
    void setBundledCaFile(const QString& path);
    Q_INVOKABLE QString autoPrivateKeyPath(const QString& serverExe,
                                           const QString& certFile) const;

    Q_INVOKABLE void connectToServer(const QString& serverIp, int serverPort,
                                     const QString& username, const QString& userCode,
                                     const QString& caFile = {});
    void connectToServerWithTlsName(const QString& serverIp, int serverPort,
                                    const QString& username, const QString& userCode,
                                    const QString& caFile, const QString& tlsServerName);
    Q_INVOKABLE void connectToLocalHost(const QString& serverExe,
                                        const QString& certFile,
                                        const QString& keyFile,
                                        const QString& dbFile,
                                        const QString& username,
                                        const QString& userCode);
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE void sendChatMessage(const QString& content);
    Q_INVOKABLE void sendRoomMessage(const QString& content, const QString& room);
    Q_INVOKABLE void sendPrivateMessage(const QString& content, const QString& targetUserCode);
    Q_INVOKABLE void requestUsers();
    Q_INVOKABLE void requestRooms();
    Q_INVOKABLE void loadMoreHistory();
    Q_INVOKABLE void setSidebarQuery(const QString& query);
    Q_INVOKABLE void createRoom(const QString& room, bool isPrivate);
    Q_INVOKABLE void sendRoomAction(const QString& action, const QString& room, const QString& targetUserCode = {});
    Q_INVOKABLE void selectRoom(const QString& room);
    Q_INVOKABLE void selectDirectMessage(const QString& userCode);
    Q_INVOKABLE void openPrivateChat(const QString& displayName, const QString& userCode);
    Q_INVOKABLE void sendAdminAction(const QString& action, const QString& targetUserCode, const QString& messageId = {});
    Q_INVOKABLE void copyText(const QString& text);
    Q_INVOKABLE void removeLocalMessage(const QString& messageId);
    Q_INVOKABLE bool recallMessage(const QString& messageId, const QString& commandId = {});

signals:
    void connectedChanged();
    void reconnectingChanged();
    void adminChanged();
    void localIdentityChanged();
    void onlineMemberCountChanged();
    void statusTextChanged();
    void activeMessageModelChanged();
    void activeRoomCanManageChanged();
    void savedConnectionChanged();
    void pendingConnectionApprovalsChanged();
    void connectionFailed(const QString& reason);
    void connectionLost(const QString& reason);
    void recallSucceeded(const QString& commandId);
    void recallFailed(const QString& commandId, const QString& reason);

private slots:
    void handleConnected(bool isAdmin);
    void handleConnectionFailed(const QString& reason);
    void handleConnectionLost(const QString& reason);
    void handleReconnectScheduled(int attempt, int delayMs);
    void handleReconnectAttempt(int attempt);
    void handleReconnectFailed(const QString& reason);
    void handleHistory(const QString& room, const QString& targetUserCode,
                       bool isPrivate, const QVariantList& messages, bool hasMore);
    void handleMessage(const QString& type, const QString& messageId, const QString& commandId,
                       const QString& username, const QString& userCode, const QString& content,
                       const QString& room, const QString& targetUserCode,
                       const QStringList& users, const QStringList& rooms,
                       const QVariantList& userDetails, const QVariantList& roomDetails,
                       bool isAdmin);

private:
    void setStatus(const QString& status);
    void appendSystemMessage(const QString& content);
    void appendSystemMessageToModel(ChatListModel* model, const QString& content);
    void incrementUnreadForConversation(const QString& key, const QString& username,
                                        const QString& userCode);
    ChatListModel* ensureConversationModel(const QString& key);
    void resetSessionData();
    bool canRecallMessage(const QString& messageId) const;
    void requestActiveHistory(const QString& beforeMessageId = {});
    void saveConnectionPreferences(const QString& serverIp, int serverPort,
                                   const QString& username, const QString& userCode,
                                   const QString& caFile);

    ChatListModel* roomModel_;
    ChatListModel* directMessageModel_;
    ChatListModel* messageModel_;
    ChatListModel* memberModel_;
    ConversationFilterModel* roomFilterModel_;
    ConversationFilterModel* directMessageFilterModel_;
    QHash<QString, ChatListModel*> conversationModels_;
    QHash<QString, int> roomMemberCounts_;
    QString activeConversationKey_ = QStringLiteral("room:lobby");
    QVariantList pendingConnectionApprovals_;
    bool activeRoomCanManage_ = false;
    QThread workerThread_;
    QTimer refreshTimer_;
    GuiConnectionWorker* worker_ = nullptr;
    bool connected_ = false;
    bool reconnecting_ = false;
    bool admin_ = false;
    QString localUserName_ = QStringLiteral("Alice");
    QString statusText_ = QStringLiteral("未连接");
    QString localUserCode_ = QStringLiteral("A001");
    QString joinedRoom_ = QStringLiteral("lobby");
    QString bundledCaFile_;
    int onlineMemberCount_ = 0;
    int localMessageCounter_ = 0;
    bool historyHasMore_ = false;
    bool historyLoading_ = false;
};
