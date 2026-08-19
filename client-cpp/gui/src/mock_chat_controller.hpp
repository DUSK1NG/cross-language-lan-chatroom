#pragma once

#include "chat_model.hpp"

#include <QObject>

class MockChatController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(ChatListModel* roomModel READ roomModel CONSTANT)
    Q_PROPERTY(ChatListModel* directMessageModel READ directMessageModel CONSTANT)
    Q_PROPERTY(ChatListModel* messageModel READ messageModel CONSTANT)
    Q_PROPERTY(ChatListModel* memberModel READ memberModel CONSTANT)

public:
    explicit MockChatController(QObject* parent = nullptr);

    ChatListModel* roomModel() const { return roomModel_; }
    ChatListModel* directMessageModel() const { return directMessageModel_; }
    ChatListModel* messageModel() const { return messageModel_; }
    ChatListModel* memberModel() const { return memberModel_; }

    Q_INVOKABLE void sendMockMessage(const QString& content);

private:
    ChatListModel* roomModel_;
    ChatListModel* directMessageModel_;
    ChatListModel* messageModel_;
    ChatListModel* memberModel_;
};
