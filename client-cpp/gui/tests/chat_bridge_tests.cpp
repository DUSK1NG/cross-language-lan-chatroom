#include "chat_bridge.hpp"
#include "gui_chat_controller.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

class ChatBridgeTests final : public QObject {
    Q_OBJECT

private slots:
    void initialSnapshotHasSchemaAndDisconnectedState();
    void snapshotContainsRoomAndMemberRoles();
    void dispatchSelectRoomCallsController();
    void invalidJsonEmitsCommandResultWithoutCallingController();
    void passwordDoesNotAppearInSnapshotOrResult();
    void modelChangesAreCoalescedIntoOneStateChangedSignal();
    void successfulConnectionClearsPreviousError();
};

void ChatBridgeTests::initialSnapshotHasSchemaAndDisconnectedState() {
    GuiChatController controller;
    ChatBridge bridge(&controller);

    const QJsonObject state = QJsonDocument::fromJson(
        bridge.currentStateJson().toUtf8()).object();
    QCOMPARE(state.value("schemaVersion").toInt(), 1);
    QCOMPARE(state.value("connection").toObject().value("phase").toString(),
             QStringLiteral("idle"));
    QCOMPARE(state.value("identity").toObject().value("displayName").toString(),
             QStringLiteral("Alice"));
    QCOMPARE(state.value("identity").toObject().value("userCode").toString(),
             QStringLiteral("A001"));
    QCOMPARE(state.value("navigation").toObject().value("activeConversation")
                 .toObject().value("kind").toString(), QStringLiteral("room"));
}

void ChatBridgeTests::snapshotContainsRoomAndMemberRoles() {
    GuiChatController controller;
    ChatBridge bridge(&controller);
    controller.roomModel()->append({{"roomName", "study"}, {"memberCount", 2},
                                    {"unreadCount", 1}, {"private", false},
                                    {"canManage", true}});
    controller.memberModel()->append({{"displayName", "Bob"}, {"userCode", "B001"},
                                      {"online", true}, {"admin", false}});

    QTRY_VERIFY(bridge.currentStateJson().contains(QStringLiteral("study")));
    const QJsonObject state = QJsonDocument::fromJson(
        bridge.currentStateJson().toUtf8()).object();
    const QJsonObject room = state.value("rooms").toArray().at(0).toObject();
    const QJsonObject member = state.value("members").toArray().at(0).toObject();
    QCOMPARE(room.value("roomName").toString(), QStringLiteral("study"));
    QCOMPARE(room.value("memberCount").toInt(), 2);
    QCOMPARE(room.value("canManage").toBool(), true);
    QCOMPARE(member.value("displayName").toString(), QStringLiteral("Bob"));
    QCOMPARE(member.value("userCode").toString(), QStringLiteral("B001"));
}

void ChatBridgeTests::dispatchSelectRoomCallsController() {
    GuiChatController controller;
    ChatBridge bridge(&controller);
    QSignalSpy activeModelSpy(&controller, &GuiChatController::activeMessageModelChanged);

    bridge.dispatch(QStringLiteral(
        R"({"id":"cmd-1","type":"conversation.selectRoom","payload":{"room":"study"}})"));

    QVERIFY(activeModelSpy.count() > 0);
    QVERIFY(controller.activeMessageModel() != nullptr);
}

void ChatBridgeTests::invalidJsonEmitsCommandResultWithoutCallingController() {
    GuiChatController controller;
    ChatBridge bridge(&controller);
    QSignalSpy resultSpy(&bridge, &ChatBridge::commandResult);

    bridge.dispatch(QStringLiteral("not-json"));

    QCOMPARE(resultSpy.count(), 1);
    const QJsonObject result = QJsonDocument::fromJson(
        resultSpy.at(0).at(0).toString().toUtf8()).object();
    QCOMPARE(result.value("id").toString(), QString());
    QCOMPARE(result.value("ok").toBool(), false);
    QCOMPARE(result.value("error").toObject().value("code").toString(),
             QStringLiteral("invalid_command"));
}

void ChatBridgeTests::passwordDoesNotAppearInSnapshotOrResult() {
    GuiChatController controller;
    ChatBridge bridge(&controller);
    QSignalSpy resultSpy(&bridge, &ChatBridge::commandResult);

    bridge.dispatch(QStringLiteral(
        R"({"id":"cmd-2","type":"session.connectRemote","payload":{"serverIp":"127.0.0.1","serverPort":8888,"username":"Alice","userCode":"A001","password":"secret","caFile":"","registerAccount":false}})"));

    QVERIFY(!bridge.currentStateJson().contains(QStringLiteral("secret")));
    QVERIFY(resultSpy.count() <= 1);
    if (resultSpy.count() == 1) {
        QVERIFY(!resultSpy.at(0).at(0).toString().contains(QStringLiteral("secret")));
    }
}

void ChatBridgeTests::modelChangesAreCoalescedIntoOneStateChangedSignal() {
    GuiChatController controller;
    ChatBridge bridge(&controller);
    QSignalSpy stateSpy(&bridge, &ChatBridge::stateChanged);

    controller.roomModel()->append({{"roomName", "one"}});
    controller.roomModel()->append({{"roomName", "two"}});

    QTRY_COMPARE_WITH_TIMEOUT(stateSpy.count(), 1, 500);
}

void ChatBridgeTests::successfulConnectionClearsPreviousError() {
    GuiChatController controller;
    ChatBridge bridge(&controller);

    QVERIFY(QMetaObject::invokeMethod(&controller, "handleConnectionFailed",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("temporary"))));
    QTRY_VERIFY(bridge.currentStateJson().contains(QStringLiteral("connection_failed")));

    QVERIFY(QMetaObject::invokeMethod(&controller, "handleConnected",
                                      Qt::DirectConnection, Q_ARG(bool, false)));
    QTRY_VERIFY(QJsonDocument::fromJson(bridge.currentStateJson().toUtf8())
                    .object().value("connection").toObject().value("phase").toString()
                    == QStringLiteral("connected"));
    const QJsonObject connection = QJsonDocument::fromJson(
        bridge.currentStateJson().toUtf8()).object().value("connection").toObject();
    QVERIFY(!connection.contains(QStringLiteral("lastError")));
}

QTEST_MAIN(ChatBridgeTests)

#include "chat_bridge_tests.moc"
