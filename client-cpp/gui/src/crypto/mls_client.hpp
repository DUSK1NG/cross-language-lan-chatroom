#pragma once

#include <mls/session.h>

#include <optional>

class MlsClient final {
public:
  struct Commit {
    MLS_NAMESPACE::bytes_ns::bytes welcome;
    MLS_NAMESPACE::bytes_ns::bytes handshake;
  };

  static MlsClient create(const MLS_NAMESPACE::bytes_ns::bytes& identity);
  MLS_NAMESPACE::bytes_ns::bytes keyPackage() const;
  void createGroup(const MLS_NAMESPACE::bytes_ns::bytes& groupId);
  MLS_NAMESPACE::bytes_ns::bytes addMember(const MLS_NAMESPACE::bytes_ns::bytes& keyPackage);
  void handleProposal(const MLS_NAMESPACE::bytes_ns::bytes& proposal);
  MLS_NAMESPACE::bytes_ns::bytes removeMember(const MLS_NAMESPACE::bytes_ns::bytes& identity);
  Commit commit();
  void handleCommit(const MLS_NAMESPACE::bytes_ns::bytes& handshake);
  void join(const MLS_NAMESPACE::bytes_ns::bytes& welcome);
  MLS_NAMESPACE::bytes_ns::bytes protect(const MLS_NAMESPACE::bytes_ns::bytes& plaintext);
  MLS_NAMESPACE::bytes_ns::bytes unprotect(const MLS_NAMESPACE::bytes_ns::bytes& ciphertext);

private:
  explicit MlsClient(const MLS_NAMESPACE::bytes_ns::bytes& identity);
  MLS_NAMESPACE::Client client_;
  std::optional<MLS_NAMESPACE::Session> session_;
  std::optional<MLS_NAMESPACE::PendingJoin> pendingJoin_;
};
