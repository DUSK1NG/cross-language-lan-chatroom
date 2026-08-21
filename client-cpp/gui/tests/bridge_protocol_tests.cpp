#include "bridge_protocol.hpp"

#include <QJsonObject>
#include <QtTest>

class BridgeProtocolTests final : public QObject {
    Q_OBJECT

private slots:
    void acceptsRoomMessageCommand();
    void rejectsMissingIdAndPayload();
    void rejectsWrongPayloadType();
    void rejectsEmptyRequiredPayloadValue();
    void neverSerializesPassword();
    void createsStableErrorEnvelope();
};

void BridgeProtocolTests::acceptsRoomMessageCommand() {
    const QJsonObject command{
        {"id", "cmd-42"},
        {"type", "chat.sendRoom"},
        {"payload", QJsonObject{{"content", "你好"}, {"room", "lobby"}}}
    };

    QString error;
    QVERIFY(bridge::validateCommand(command, &error));
    QVERIFY(error.isEmpty());
}

void BridgeProtocolTests::rejectsMissingIdAndPayload() {
    QString error;
    QVERIFY(!bridge::validateCommand(QJsonObject{{"type", "chat.sendRoom"}}, &error));
    QCOMPARE(error, QStringLiteral("invalid_command"));
}

void BridgeProtocolTests::rejectsWrongPayloadType() {
    const QJsonObject command{
        {"id", "cmd-43"},
        {"type", "chat.sendRoom"},
        {"payload", QStringLiteral("not-an-object")}
    };

    QString error;
    QVERIFY(!bridge::validateCommand(command, &error));
    QCOMPARE(error, QStringLiteral("invalid_command"));
}

void BridgeProtocolTests::rejectsEmptyRequiredPayloadValue() {
    const QJsonObject command{
        {"id", "cmd-44"},
        {"type", "chat.sendRoom"},
        {"payload", QJsonObject{{"content", " "}, {"room", "lobby"}}}
    };

    QString error;
    QVERIFY(!bridge::validateCommand(command, &error));
    QCOMPARE(error, QStringLiteral("invalid_command"));
}

void BridgeProtocolTests::neverSerializesPassword() {
    const QString json = bridge::serializeState(QJsonObject{
        {"connection", QJsonObject{{"phase", "connected"}}},
        {"savedConnection", QJsonObject{{"username", "Alice"}, {"password", "secret"}}},
        {"host", QJsonObject{{"privateKey", "secret-key"}}}
    });

    QVERIFY(!json.contains(QStringLiteral("password"), Qt::CaseInsensitive));
    QVERIFY(!json.contains(QStringLiteral("privateKey"), Qt::CaseInsensitive));
}

void BridgeProtocolTests::createsStableErrorEnvelope() {
    const QJsonObject error = bridge::makeError(
        QStringLiteral("connection_failed"), QStringLiteral("无法连接到服务器"), true,
        QStringLiteral("controller"), QStringLiteral("cmd-42"));

    QCOMPARE(error.value("code").toString(), QStringLiteral("connection_failed"));
    QCOMPARE(error.value("message").toString(), QStringLiteral("无法连接到服务器"));
    QCOMPARE(error.value("retryable").toBool(), true);
    QCOMPARE(error.value("source").toString(), QStringLiteral("controller"));
    QCOMPARE(error.value("commandId").toString(), QStringLiteral("cmd-42"));
}

QTEST_APPLESS_MAIN(BridgeProtocolTests)

#include "bridge_protocol_tests.moc"
