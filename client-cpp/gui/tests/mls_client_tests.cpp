#include "crypto/mls_client.hpp"

#include <QtTest>

using MLS_NAMESPACE::bytes_ns::bytes;

class MlsClientTests final : public QObject {
  Q_OBJECT

private slots:
  void createsKeyPackageAndRoundTripsApplicationData();
  void rejectsTamperedCiphertext();
  void rejectsTamperedHandshake();
  void handlesRemoteHandshakeCommit();
  void createsRemovalCommit();
};

void MlsClientTests::createsKeyPackageAndRoundTripsApplicationData()
{
  auto alice = MlsClient::create({ 1, 2, 3, 4 });
  auto bob = MlsClient::create({ 5, 6, 7, 8 });
  QVERIFY(!bob.keyPackage().empty());

  alice.createGroup({ 9, 10, 11, 12 });
  alice.addMember(bob.keyPackage());
  const auto commit = alice.commit();
  bob.join(commit.welcome);

  const bytes plaintext{ 0, 1, 2, 3 };
  QCOMPARE(bob.unprotect(alice.protect(plaintext)), plaintext);
}

void MlsClientTests::rejectsTamperedCiphertext()
{
  auto alice = MlsClient::create({ 1, 2, 3, 4 });
  auto bob = MlsClient::create({ 5, 6, 7, 8 });
  alice.createGroup({ 9, 10, 11, 12 });
  alice.addMember(bob.keyPackage());
  const auto commit = alice.commit();
  bob.join(commit.welcome);

  auto ciphertext = alice.protect({ 42 });
  ciphertext.at(0) ^= 0x01;
  QVERIFY_EXCEPTION_THROWN(bob.unprotect(ciphertext), std::exception);
}

void MlsClientTests::rejectsTamperedHandshake()
{
  auto alice = MlsClient::create({ 1, 2, 3, 4 });
  auto bob = MlsClient::create({ 5, 6, 7, 8 });
  alice.createGroup({ 9, 10, 11, 12 });
  alice.addMember(bob.keyPackage());
  const auto firstCommit = alice.commit();
  bob.join(firstCommit.welcome);

  const auto update = alice.commit();
  auto handshake = update.handshake;
  handshake.at(0) ^= 0x01;
  QVERIFY_EXCEPTION_THROWN(bob.handleCommit(handshake), std::exception);
}

void MlsClientTests::handlesRemoteHandshakeCommit()
{
  auto alice = MlsClient::create({ 1, 2, 3, 4 });
  auto bob = MlsClient::create({ 5, 6, 7, 8 });
  auto charlie = MlsClient::create({ 9, 10, 11, 12 });
  alice.createGroup({ 13, 14, 15, 16 });
  alice.addMember(bob.keyPackage());
  const auto firstCommit = alice.commit();
  bob.join(firstCommit.welcome);

  const auto secondProposal = alice.addMember(charlie.keyPackage());
  bob.handleProposal(secondProposal);
  const auto secondCommit = alice.commit();
  bob.handleCommit(secondCommit.handshake);
  charlie.join(secondCommit.welcome);

  const bytes plaintext{ 42, 43 };
  QCOMPARE(charlie.unprotect(bob.protect(plaintext)), plaintext);
}

void MlsClientTests::createsRemovalCommit()
{
  auto alice = MlsClient::create({ 1, 2, 3, 4 });
  auto bob = MlsClient::create({ 5, 6, 7, 8 });
  alice.createGroup({ 17, 18, 19, 20 });
  alice.addMember(bob.keyPackage());
  const auto firstCommit = alice.commit();
  bob.join(firstCommit.welcome);

  alice.removeMember({ 5, 6, 7, 8 });
  const auto removalCommit = alice.commit();
  QVERIFY(!removalCommit.handshake.empty());
}

#include "mls_client_tests.moc"

QTEST_MAIN(MlsClientTests)
