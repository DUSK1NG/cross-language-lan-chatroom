#include "crypto/mls_client.hpp"

#include <QtTest>

using MLS_NAMESPACE::bytes_ns::bytes;

class MlsClientTests final : public QObject {
  Q_OBJECT

private slots:
  void createsKeyPackageAndRoundTripsApplicationData();
  void rejectsTamperedCiphertext();
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

#include "mls_client_tests.moc"

QTEST_MAIN(MlsClientTests)
