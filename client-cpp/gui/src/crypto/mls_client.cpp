#include "crypto/mls_client.hpp"

#include <stdexcept>
#include <utility>

namespace {
MLS_NAMESPACE::CipherSuite suite()
{
  return { MLS_NAMESPACE::CipherSuite::ID::P256_AES128GCM_SHA256_P256 };
}
}

MlsClient MlsClient::create(const MLS_NAMESPACE::bytes_ns::bytes& identity)
{
  return MlsClient(identity);
}

MlsClient::MlsClient(const MLS_NAMESPACE::bytes_ns::bytes& identity)
  : client_(suite(), MLS_NAMESPACE::SignaturePrivateKey::generate(suite()),
            MLS_NAMESPACE::Credential::basic(identity))
{
  pendingJoin_ = client_.start_join();
}

MLS_NAMESPACE::bytes_ns::bytes MlsClient::keyPackage() const
{
  if (!pendingJoin_) {
    throw std::logic_error("MLS client has no pending key package");
  }
  return pendingJoin_->key_package();
}

void MlsClient::createGroup(const MLS_NAMESPACE::bytes_ns::bytes& groupId)
{
  session_ = client_.begin_session(groupId);
}

void MlsClient::addMember(const MLS_NAMESPACE::bytes_ns::bytes& keyPackage)
{
  if (!session_) {
    throw std::logic_error("MLS client has no group session");
  }
  const auto proposal = session_->add(keyPackage);
  session_->handle(proposal);
}

MlsClient::Commit MlsClient::commit()
{
  if (!session_) {
    throw std::logic_error("MLS client has no group session");
  }
  auto [welcome, handshake] = session_->commit();
  session_->handle(handshake);
  return { std::move(welcome), std::move(handshake) };
}

void MlsClient::join(const MLS_NAMESPACE::bytes_ns::bytes& welcome)
{
  if (!pendingJoin_) {
    throw std::logic_error("MLS client has no pending key package");
  }
  session_ = pendingJoin_->complete(welcome);
  pendingJoin_.reset();
}

MLS_NAMESPACE::bytes_ns::bytes MlsClient::protect(const MLS_NAMESPACE::bytes_ns::bytes& plaintext)
{
  if (!session_) {
    throw std::logic_error("MLS client has no group session");
  }
  return session_->protect(plaintext);
}

MLS_NAMESPACE::bytes_ns::bytes MlsClient::unprotect(const MLS_NAMESPACE::bytes_ns::bytes& ciphertext)
{
  if (!session_) {
    throw std::logic_error("MLS client has no group session");
  }
  return session_->unprotect(ciphertext);
}
