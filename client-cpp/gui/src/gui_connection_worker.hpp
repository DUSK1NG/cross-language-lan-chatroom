#pragma once

#include "connection.hpp"

#include <QObject>
#include <QProcess>
#include <QString>
#include <atomic>
#include <memory>
#include <thread>

class GuiConnectionWorker final : public QObject {
    Q_OBJECT

public:
    explicit GuiConnectionWorker(QObject* parent = nullptr);
    ~GuiConnectionWorker() override;

public slots:
    void connectToServer(const QString& serverIp,
                         int serverPort,
                         const QString& username,
                         const QString& userCode,
                         const QString& password,
                         const QString& caFile,
                         bool registerAccount);
    void connectToLocalHost(const QString& serverExe,
                            const QString& certFile,
                            const QString& keyFile,
                            const QString& dbFile,
                            const QString& username,
                            const QString& userCode);
    void disconnectFromServer();
    void sendChat(const QString& content);
    void sendChatToRoom(const QString& content, const QString& room);
    void sendPrivate(const QString& content, const QString& targetUserCode);
    void joinRoom(const QString& room);
    void requestUsers();
    void requestRooms();
    void sendAdminAction(const QString& action, const QString& targetUserCode, const QString& messageId = {});

signals:
    void connected(bool isAdmin);
    void connectionFailed(const QString& reason);
    void connectionLost(const QString& reason);
    void messageReceived(const QString& type,
                         const QString& messageId,
                         const QString& username,
                         const QString& userCode,
                         const QString& content,
                         const QString& room,
                         const QString& targetUserCode,
                         const QStringList& users,
                         const QStringList& rooms,
                         bool isAdmin);

private:
    void receiveLoop();
    void stopReceiveLoop();

    std::unique_ptr<connection::ConnectionState> connection_;
    std::unique_ptr<QProcess> hostProcess_;
    std::thread receiveThread_;
    std::atomic<bool> running_{false};
};
