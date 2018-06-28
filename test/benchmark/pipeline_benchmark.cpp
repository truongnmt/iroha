#include <benchmark/benchmark.h>
#include <string>

#include "backend/protobuf/transaction.hpp"
#include "builders/protobuf/unsigned_proto.hpp"
#include "cryptography/crypto_provider/crypto_defaults.hpp"
#include "datetime/time.hpp"
#include "framework/integration_framework/integration_test_framework.hpp"
#include "integration/acceptance/acceptance_fixture.hpp"
#include "module/shared_model/builders/protobuf/test_transaction_builder.hpp"
#include "validators/permissions.hpp"

TestUnsignedTransactionBuilder createUser(
    const std::string &user, const shared_model::crypto::PublicKey &key) {
  static int i = 0;
  return TestUnsignedTransactionBuilder()
      .createAccount(
          user,
          integration_framework::IntegrationTestFramework::kDefaultDomain,
          key)
      .creatorAccountId(
          integration_framework::IntegrationTestFramework::kAdminId)
      .createdTime(iroha::time::now() + i++)
      .quorum(1);
}

TestUnsignedTransactionBuilder createUserWithPerms(
    const std::string &user,
    const shared_model::crypto::PublicKey &key,
    const std::string &role_id,
    const shared_model::interface::RolePermissionSet &perms) {
  const auto user_id = user + "@"
      + integration_framework::IntegrationTestFramework::kDefaultDomain;
  return createUser(user, key)
      .detachRole(user_id,
                  integration_framework::IntegrationTestFramework::kDefaultRole)
      .createRole(role_id, perms)
      .appendRole(user_id, role_id);
}

using namespace integration_framework;
using namespace shared_model;

const std::string kUser = "user";
const std::string kUserId = kUser + "@test";
const std::string kAmount = "1.0";
const crypto::Keypair kAdminKeypair =
    crypto::DefaultCryptoAlgorithmType::generateKeypair();
const crypto::Keypair kUserKeypair =
    crypto::DefaultCryptoAlgorithmType::generateKeypair();

auto baseTx() {
  return TestUnsignedTransactionBuilder().creatorAccountId(kUserId).createdTime(
      iroha::time::now());
}

//

static void BM_AddAssetQuantity(benchmark::State &state) {
  const std::string kAsset = "coin#test";

  auto make_perms = [&] {
    return createUserWithPerms(
               kUser,
               kUserKeypair.publicKey(),
               "role",
               shared_model::interface::RolePermissionSet{
                   shared_model::interface::permissions::Role::kAddAssetQty})
        .quorum(1)
        .build()
        .signAndAddSignature(kAdminKeypair).finish();
  };

  IntegrationTestFramework itf(10);
  itf.setInitialState(kAdminKeypair);

  for (int i = 0; i < 10; i++) {
    itf.sendTx(make_perms());
  }
  itf.skipProposal().skipBlock();

  //  auto transaction = ;
  // define main benchmark loop
  while (state.KeepRunning()) {
    // define the code to be tested

    auto make_base = [&]() {
      auto base = baseTx();
      for (int i = 0; i < 10; i++) {
        base = base.addAssetQuantity(kUserId, kAsset, kAmount);
      }
      return base.quorum(1).build().signAndAddSignature(kUserKeypair).finish();
    };

    for (int i = 0; i < 10; i++) {
      itf.sendTx(make_base());
    }
    itf.skipProposal().skipBlock();
  }
  itf.done();
}
// define benchmark
BENCHMARK(BM_AddAssetQuantity)->Unit(benchmark::kMillisecond);

/// That's all. More in documentation.

// don't forget to include this:
BENCHMARK_MAIN();
