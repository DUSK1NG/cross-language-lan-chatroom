#include "mock_chat_controller.hpp"

#include <QTime>

MockChatController::MockChatController(QObject* parent)
    : QObject(parent),
      roomModel_(new ChatListModel({"roomName", "memberCount", "unreadCount"}, this)),
      directMessageModel_(new ChatListModel({"displayName", "userCode", "unreadCount"}, this)),
      messageModel_(new ChatListModel({"sender", "userCode", "time", "content", "selfMessage", "systemMessage"}, this)),
      memberModel_(new ChatListModel({"displayName", "userCode", "online", "admin"}, this)) {
    roomModel_->append({{"roomName", "lobby"}, {"memberCount", 3}, {"unreadCount", 0}});
    roomModel_->append({{"roomName", "study"}, {"memberCount", 5}, {"unreadCount", 2}});
    roomModel_->append({{"roomName", "gaming"}, {"memberCount", 4}, {"unreadCount", 0}});

    directMessageModel_->append({{"displayName", "Bob"}, {"userCode", "B002"}, {"unreadCount", 2}});
    directMessageModel_->append({{"displayName", "Chen"}, {"userCode", "C003"}, {"unreadCount", 0}});

    messageModel_->append({{"sender", "Alice"}, {"userCode", "A001"}, {"time", "18:24"},
                           {"content", "今天的学习资料整理好了吗？"}, {"selfMessage", false}, {"systemMessage", false}});
    messageModel_->append({{"sender", "Alice"}, {"userCode", "A001"}, {"time", "18:24"},
                           {"content", "我还在整理最后一部分。"}, {"selfMessage", false}, {"systemMessage", false}});
    messageModel_->append({{"sender", "Bob"}, {"userCode", "B002"}, {"time", "18:25"},
                           {"content", "我已经完成了，可以发到这里。"}, {"selfMessage", false}, {"systemMessage", false}});
    messageModel_->append({{"sender", "Mock User"}, {"userCode", "A001"}, {"time", "18:26"},
                           {"content", "好的，谢谢！"}, {"selfMessage", true}, {"systemMessage", false}});

    memberModel_->append({{"displayName", "Alice"}, {"userCode", "A001"}, {"online", true}, {"admin", true}});
    memberModel_->append({{"displayName", "Bob"}, {"userCode", "B002"}, {"online", true}, {"admin", false}});
    memberModel_->append({{"displayName", "Chen"}, {"userCode", "C003"}, {"online", true}, {"admin", false}});
}

void MockChatController::sendMockMessage(const QString& content) {
    const QString trimmed = content.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    messageModel_->append({{"sender", "Mock User"}, {"userCode", "A001"},
                           {"time", QTime::currentTime().toString("HH:mm")},
                           {"content", trimmed}, {"selfMessage", true}, {"systemMessage", false}});
}
