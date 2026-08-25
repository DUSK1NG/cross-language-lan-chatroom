#include <winsock2.h>
#include <ws2tcpip.h>

#include <QtTest>

#include <string>

namespace connection {
bool resolve_ipv4_endpoint(const std::string& host, int port, sockaddr_in* endpoint,
                           std::string* error);
}

class ConnectionEndpointTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void resolvesDnsHostnameToIpv4();

private:
    WSADATA winsock_{};
};

void ConnectionEndpointTests::initTestCase() {
    QCOMPARE(WSAStartup(MAKEWORD(2, 2), &winsock_), 0);
}

void ConnectionEndpointTests::cleanupTestCase() {
    WSACleanup();
}

void ConnectionEndpointTests::resolvesDnsHostnameToIpv4() {
    sockaddr_in endpoint{};
    std::string error;

    QVERIFY2(connection::resolve_ipv4_endpoint("localhost", 50440, &endpoint, &error),
             error.c_str());
    QCOMPARE(endpoint.sin_family, AF_INET);
    QCOMPARE(ntohs(endpoint.sin_port), static_cast<u_short>(50440));
}

QTEST_APPLESS_MAIN(ConnectionEndpointTests)

#include "connection_endpoint_tests.moc"
