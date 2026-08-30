#include <iostream>
#include <mls/session.h>

using namespace MLS_NAMESPACE;

int main()
{
  try {
    const CipherSuite suite{ CipherSuite::ID::P256_AES128GCM_SHA256_P256 };
    const bytes group_id{ 0, 1, 2, 3 };
    const bytes identity_a{ 4, 5, 6, 7 };
    const bytes identity_b{ 8, 9, 10, 11 };

    Client alice{ suite,
                  SignaturePrivateKey::generate(suite),
                  Credential::basic(identity_a) };
    Client bob{ suite,
                SignaturePrivateKey::generate(suite),
                Credential::basic(identity_b) };

    auto alice_session = alice.begin_session(group_id);
    auto pending_bob = bob.start_join();
    const auto add_proposal = alice_session.add(pending_bob.key_package());
    alice_session.handle(add_proposal);

    const auto [welcome, commit] = alice_session.commit();
    alice_session.handle(commit);
    auto bob_session = pending_bob.complete(welcome);

    const bytes plaintext{ 0, 1, 2, 3, 4 };
    const auto ciphertext = alice_session.protect(plaintext);
    if (bob_session.unprotect(ciphertext) != plaintext) {
      std::cerr << "MLS++ smoke: protect/unprotect mismatch\n";
      return 1;
    }

    std::cout << "MLS++ API smoke passed: Session/KeyPackage/protect/unprotect\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "MLS++ smoke exception: " << error.what() << '\n';
    return 1;
  }
}
