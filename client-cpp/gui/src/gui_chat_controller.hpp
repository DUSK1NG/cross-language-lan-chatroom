#pragma once

#include "chat_model.hpp"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QStringList>

class GuiConnectionWorker;

class GuiChatController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(ChatListModel* roomModel READ roomModel CONSTANT)
    Q_PROPERTY(ChatListModel* directMessageModel READ directMessageModel CONSTANT)
    Q_PROPERTY(ChatListModel* messageModel READ messageModel CONSTANT)
    Q_PROPERTY(ChatListModel* activeMessageModel READ activeMessageModel NOTIFY activeMessageModelChanged)
    Q_PROPERTY(ChatListModel* memberModel READ memberModel CONSTANT)
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
    bool connected() const { return connected_; }
    bool reconnecting() const { return reconnecting_; }
    bool admin() const { return admin_; }
    QString localUserName() const { return localUserName_; }
    QString localUserCode() const { return localUserCode_; }
    int onlineMemberCount() const { return onlineMemberCount_; }
    QString statusText() const { return statusText_; }
    bool activeRoomCanManage() const { return activeRoomCanManage_; }
    QString activeConversationKey() const { return activeConversationKey_; }
    int cachedConversationModelCount() const { return conversationModels_.size(); }
    QString savedServerIp() const;
    int savedServerPort() const;
    QString savedUsername() const;
    QString savedUserCode() const;
    QString savedCaFile() const;
    QVariantList pendingConnectionApprovals() const { return pendingConnectionApprovals_; }
    void setBundledCaFile(const QString& path);

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
    Q_INVOKABLE void sendRoomMessage(const QString& content, const QString& room);
    Q_INVOKABLE void sendPrivateMessage(const QString& content, const QString& targetUserCode);
    Q_INVOKABLE void requestUsers();
    Q_INVOKABLE void requestRooms();
    Q_INVOKABLE void searchActiveHistory(const QString& query);
    Q_INVOKABLE void createRoom(const QString& room, bool isPrivate);
    Q_INVOKABLE void sendRoomAction(const QString& action, const QString& room, const QString& targetUserCode = {});
    Q_INVOKABLE void selectRoom(const QString& room);
    Q_INVOKABLE void selectDirectMessage(const QString& userCode);
    Q_INVOKABLE void openPrivateChat(const QString& displayName, const QString& userCode);
    Q_INVOKABLE void sendAdminAction(const QString& action, const QString& targetUserCode, const QString& messageId = {});
    Q_INVOKABLE void copyText(const QString& text);
    Q_INVOKABLE void removeLocalMessage(const QString& messageId);
    Q_INVOKABLE bool recallMessage(const QString& messageId, const QString& commandId = {});
    Q_INVOKABLE bool retryMessage(const QString& messageId);
    Q_INVOKABLE void sendAttachmentInit(const QString& room, qint64 logicalSize, const QString& commandId);
    Q_INVOKABLE void startAttachmentUpload(const QString& room, const QString& filePath,
                                           const QStringList& targetUsers, const QString& commandId);
    Q_INVOKABLE void chooseAndUploadAttachment(const QString& room, const QString& commandId);
    Q_INVOKABLE void sendAttachmentChunk(const QString& uploadId, qint64 chunkIndex,
                                         const QByteArray& ciphertext, const QByteArray& cipherSha256,
                                         const QString& commandId);
    Q_INVOKABLE void resumeAttachment(const QString& uploadId, const QString& commandId);
    Q_INVOKABLE void sendAttachmentCommit(const QString& uploadId, const QString& commandId);
    Q_INVOKABLE void sendAttachmentDownload(const QString& attachmentId, qint64 chunkIndex,
                                             const QString& commandId);
    Q_INVOKABLE void startAttachmentDownload(const QString& attachmentId, const QString& outputPath,
                                             const QString& commandId);
    Q_INVOKABLE void fetchMlsKeyPackage(const QString& room, const QString& targetUserCode,
                                        const QString& commandId = {});
    Q_INVOKABLE void addMlsMember(const QString& room, const QString& groupId,
                                  const QString& targetUserCode, const QString& commandId = {});
    Q_INVOKABLE void removeMlsMember(const QString& room, const QString& groupId,
                                     const QString& targetUserCode, const QString& commandId = {});
    Q_INVOKABLE void inspectMlsGroup(const QString& groupId, const QString& commandId = {});
    Q_INVOKABLE void protectMls(const QString& groupId, const QByteArray& plaintext, const QString& commandId = {});
    Q_INVOKABLE void unprotectMls(const QString& groupId, const QByteArray& ciphertext, const QString& commandId = {});
#if defined(LAN_CHAT_ENABLE_MLSPP) && defined(LAN_CHAT_ENABLE_TEST_HOOKS)
    void enableDropNextMlsCommitForTesting();
#endif

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
    void mlsCommandResult(const QString& commandId, bool ok,
                          const QString& code, const QString& message);
    void mlsKeyPackageAvailable(const QString& targetUserCode, const QString& digest);
    void mlsWelcomeEvent(const QString& groupId, quint64 epoch, const QString& phase);
    void mlsGroupState(const QString& commandId, bool ok, const QString& groupId,
                       quint64 epoch, const QString& message);
    void mlsDataResult(const QString& commandId, bool ok, const QByteArray& data,
                       const QString& message);
    void attachmentEvent(const QString& type, const QString& attachmentId,
                         const QString& uploadId, const QString& commandId,
                         qint64 chunkSize, qint64 chunkIndex,
                         const QByteArray& ciphertext, const QByteArray& cipherSha256,
                         const QString& expiresAt, const QList<qint64>& receivedIndexes,
                         const QString& content, qint64 logicalSize = 0);

private slots:
    void handleConnected(bool isAdmin);
    void handleConnectionFailed(const QString& reason);
    void handleConnectionLost(const QString& reason);
    void handleReconnectScheduled(int attempt, int delayMs);
    void handleReconnectAttempt(int attempt);
    void handleReconnectFailed(const QString& reason);
    void handleHistory(const QString& room, const QString& targetUserCode,
                       bool isPrivate, const QVariantList& messages, bool hasMore,
                       const QString& searchQuery);
    void handleMessage(const QString& type, const QString& messageId, const QString& commandId,
                       const QString& username, const QString& userCode, const QString& content,
                       const QString& room, const QString& targetUserCode, const QString& deliveryState,
                       const QStringList& users, const QStringList& rooms,
                       const QVariantList& userDetails, const QVariantList& roomDetails,
                       bool isAdmin);
    void handleAttachmentEvent(const QString& type, const QString& attachmentId,
                               const QString& uploadId, const QString& commandId,
                               qint64 chunkSize, qint64 chunkIndex,
                               const QByteArray& ciphertext, const QByteArray& cipherSha256,
                               const QString& expiresAt, const QList<qint64>& receivedIndexes,
                               const QString& content);

private:
    void setStatus(const QString& status);
    void appendSystemMessage(const QString& content);
    void appendSystemMessageToModel(ChatListModel* model, const QString& content);
    void incrementUnreadForConversation(const QString& key, const QString& username,
                                        const QString& userCode);
    ChatListModel* ensureConversationModel(const QString& key);
    void trimConversationModelCache(const QString& protectedKey);
    void resetSessionData();
    bool canRecallMessage(const QString& messageId) const;
    void requestActiveHistory(const QString& beforeMessageId = {}, const QString& searchQuery = {});
    void saveConnectionPreferences(const QString& serverIp, int serverPort,
                                   const QString& username, const QString& userCode,
                                   const QString& caFile);

    ChatListModel* roomModel_;
    ChatListModel* directMessageModel_;
    ChatListModel* messageModel_;
    ChatListModel* memberModel_;
    QHash<QString, ChatListModel*> conversationModels_;
    QHash<QString, quint64> conversationModelAccessOrder_;
    quint64 conversationModelAccessSequence_ = 0;
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
    bool historyLoading_ = false;
    QString historySearchQuery_;
    QHash<QString, QVariantMap> pendingAttachmentMetadata_;
};
