#include "reconnect_policy.hpp"

#include <QtTest>

class ReconnectPolicyTests final : public QObject {
    Q_OBJECT

private slots:
    void schedulesBoundedExponentialDelays();
    void resetsAfterSuccessfulLogin();
    void stopsAfterExplicitDisconnect();
};

void ReconnectPolicyTests::schedulesBoundedExponentialDelays() {
    ReconnectPolicy policy;

    QCOMPARE(policy.scheduleNextAttempt(), 500);
    QCOMPARE(policy.scheduleNextAttempt(), 1000);
    QCOMPARE(policy.scheduleNextAttempt(), 2000);
    QCOMPARE(policy.scheduleNextAttempt(), 4000);
    QCOMPARE(policy.scheduleNextAttempt(), 8000);
    QCOMPARE(policy.scheduleNextAttempt(), 8000);
    QCOMPARE(policy.attemptCount(), 6);
}

void ReconnectPolicyTests::resetsAfterSuccessfulLogin() {
    ReconnectPolicy policy;
    policy.scheduleNextAttempt();
    policy.scheduleNextAttempt();

    policy.markConnected();

    QCOMPARE(policy.attemptCount(), 0);
    QCOMPARE(policy.scheduleNextAttempt(), 500);
}

void ReconnectPolicyTests::stopsAfterExplicitDisconnect() {
    ReconnectPolicy policy;
    policy.scheduleNextAttempt();

    policy.cancel();

    QVERIFY(!policy.isActive());
    QCOMPARE(policy.scheduleNextAttempt(), -1);
}

QTEST_GUILESS_MAIN(ReconnectPolicyTests)

#include "reconnect_policy_tests.moc"
