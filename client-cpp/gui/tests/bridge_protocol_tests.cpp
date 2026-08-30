#include "bridge_protocol.hpp"

#include <QJsonObject>
#include <QtTest>

class BridgeProtocolTests final : public QObject {
    Q_OBJECT

private slots:
    void acceptsRoomMessageCommand();
    void acceptsOpaqueEncryptedRoomMessageCommand();
    void acceptsHistorySearchCommand();
    void acceptsPerformanceModeCommand();
    void acceptsConnectionLogPreferenceCommand();
    void acceptsBoundedFrameTelemetryCommand();
    void acceptsRemoteConnectionWithoutCaFile();
    void rejectsTunnelTlsIdentityOtherThanLocalhost();
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

void BridgeProtocolTests::acceptsOpaqueEncryptedRoomMessageCommand() {
    const QJsonObject command{
        {"id", "cmd-e2ee"},
        {"type", "chat.sendRoom"},
        {"payload", QJsonObject{{"room", "lobby"},
                                 {"protocol_version", "2"},
                                 {"capabilities", QJsonArray{"e2ee-envelope-v1"}},
                                 {"crypto", QJsonObject{{"v", 1}, {"alg", "xchacha20poly1305"}, {"ct", "opaque"}}}}}
    };

    QString error;
    QVERIFY(bridge::validateCommand(command, &error));
    QVERIFY(error.isEmpty());
}

void BridgeProtocolTests::acceptsHistorySearchCommand() {
    const QJsonObject command{
        {"id", "cmd-history-search"},
        {"type", "history.search"},
        {"payload", QJsonObject{{"query", "你好"}}}
    };

    QString error;
    QVERIFY(bridge::validateCommand(command, &error));
    QVERIFY(error.isEmpty());
}

void BridgeProtocolTests::acceptsPerformanceModeCommand() {
    const QJsonObject command{
        {"id", "cmd-performance"},
        {"type", "settings.setPerformanceMode"},
        {"payload", QJsonObject{{"mode", "Power Saving"}}}
    };

    QString error;
    QVERIFY(bridge::validateCommand(command, &error));
    QVERIFY(error.isEmpty());
}

void BridgeProtocolTests::acceptsConnectionLogPreferenceCommand() {
    const QJsonObject command{
        {"id", "cmd-connection-log"},
        {"type", "settings.setConnectionLogging"},
        {"payload", QJsonObject{{"enabled", true}}}
    };

    QString error;
    QVERIFY(bridge::validateCommand(command, &error));
    QVERIFY(error.isEmpty());
}

void BridgeProtocolTests::acceptsBoundedFrameTelemetryCommand() {
    const QJsonObject command{
        {"id", "cmd-frame-times"},
        {"type", "performance.reportFrameTimes"},
        {"payload", QJsonObject{{"frameTimesMs", QJsonArray{16.0, 17.0, 33.0}}}}
    };

    QString error;
    QVERIFY(bridge::validateCommand(command, &error));
    QVERIFY(error.isEmpty());
}

void BridgeProtocolTests::acceptsRemoteConnectionWithoutCaFile() {
    const QJsonObject command{
        {"id", "cmd-remote"},
        {"type", "session.connectRemote"},
        {"payload", QJsonObject{{"serverIp", "127.0.0.1"},
                                 {"serverPort", 8888},
                                 {"username", "Alice"},
                                 {"userCode", "A001"},
                                 {"caFile", ""}}}
    };

    QString error;
    QVERIFY(bridge::validateCommand(command, &error));
    QVERIFY(error.isEmpty());
}

void BridgeProtocolTests::rejectsTunnelTlsIdentityOtherThanLocalhost() {
    const QJsonObject command{
        {"id", "cmd-tunnel"},
        {"type", "session.connectRemote"},
        {"payload", QJsonObject{{"serverIp", "frp-bus.com"},
                                 {"serverPort", 50440},
                                 {"username", "Bob"},
                                 {"userCode", "B001"},
                                 {"caFile", "server-lan.crt"},
                                 {"tlsServerName", "frp-bus.com"}}}
    };

    QString error;
    QVERIFY(!bridge::validateCommand(command, &error));
    QCOMPARE(error, QStringLiteral("invalid_command"));
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
