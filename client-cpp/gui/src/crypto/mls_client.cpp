#include "crypto/mls_client.hpp"

#include <cstdint>
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

MLS_NAMESPACE::bytes_ns::bytes MlsClient::addMember(const MLS_NAMESPACE::bytes_ns::bytes& keyPackage)
{
  if (!session_) {
    throw std::logic_error("MLS client has no group session");
  }
  const auto proposal = session_->add(keyPackage);
  session_->handle(proposal);
  return proposal;
}

void MlsClient::handleProposal(const MLS_NAMESPACE::bytes_ns::bytes& proposal)
{
  if (!session_) {
    throw std::logic_error("MLS client has no group session");
  }
  session_->handle(proposal);
}

MLS_NAMESPACE::bytes_ns::bytes MlsClient::removeMember(const MLS_NAMESPACE::bytes_ns::bytes& identity)
{
  if (!session_) {
    throw std::logic_error("MLS client has no group session");
  }
  const auto roster = session_->roster();
  for (std::uint32_t index = 0; index < roster.size(); ++index) {
    const auto& credential = roster[index].credential;
    if (credential.type() != MLS_NAMESPACE::CredentialType::basic) {
      continue;
    }
    if (credential.get<MLS_NAMESPACE::BasicCredential>().identity == identity) {
      const auto proposal = session_->remove(index);
      session_->handle(proposal);
      return proposal;
    }
  }
  throw std::invalid_argument("MLS member identity was not found");
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

void MlsClient::handleCommit(const MLS_NAMESPACE::bytes_ns::bytes& handshake)
{
  if (!session_) {
    throw std::logic_error("MLS client has no group session");
  }
  if (!session_->handle(handshake)) {
    throw std::runtime_error("MLS handshake was rejected");
  }
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
