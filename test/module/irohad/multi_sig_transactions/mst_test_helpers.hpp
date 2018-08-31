/**
 * Copyright Soramitsu Co., Ltd. 2017 All Rights Reserved.
 * http://soramitsu.co.jp
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef IROHA_MST_TEST_HELPERS_HPP
#define IROHA_MST_TEST_HELPERS_HPP

#include <string>
#include "builders/protobuf/common_objects/proto_peer_builder.hpp"
#include "builders/protobuf/transaction.hpp"
#include "cryptography/crypto_provider/crypto_defaults.hpp"
#include "datetime/time.hpp"
#include "interfaces/common_objects/types.hpp"
#include "multi_sig_transactions/mst_types.hpp"

inline auto makeKey() {
  return shared_model::crypto::DefaultCryptoAlgorithmType::generateKeypair();
}

inline auto txBuilder(
    const shared_model::interface::types::CounterType &counter,
    iroha::TimeType created_time = iroha::time::now(),
    shared_model::interface::types::QuorumType quorum = 3,
    shared_model::interface::types::AccountIdType account_id = "user@test") {
  return TestTransactionBuilder()
      .createdTime(created_time)
      .creatorAccountId(account_id)
      .setAccountQuorum(account_id, counter)
      .quorum(quorum);
}

template <typename... TxBuilders>
auto makeTestBatch(TxBuilders... builders) {
  return framework::batch::makeTestBatch(builders...);
}

template <typename Batch, typename... Signatures>
auto addSignatures(Batch &&batch, int tx_number, Signatures... signatures) {
  auto insert_signatures = [&](auto &&sig_pair) {
    batch->addSignature(tx_number, sig_pair.first, sig_pair.second);
  };

  // pack expansion trick:
  // an ellipsis operator applies insert_signatures to each signature, operator
  // comma returns the rightmost argument, which is 0
  int temp[] = {
      (insert_signatures(std::forward<Signatures>(signatures)), 0)...};
  // use unused variable
  (void)temp;

  mst_helpers_log_->info(
      "Number of signatures was inserted {}",
      boost::size(batch->transactions().at(tx_number)->signatures()));
  return batch;
}

template <typename Batch, typename... KeyPairs>
auto addSignaturesFromKeyPairs(Batch &&batch,
                               int tx_number,
                               KeyPairs... keypairs) {
  auto create_signature = [&](auto &&key_pair) {
    auto &payload = batch->transactions().at(tx_number)->payload();
    auto signed_blob = shared_model::crypto::CryptoSigner<>::sign(
        shared_model::crypto::Blob(payload), key_pair);
    batch->addSignature(tx_number, signed_blob, key_pair.publicKey());
  };

  // pack expansion trick:
  // an ellipsis operator applies insert_signatures to each signature, operator
  // comma returns the rightmost argument, which is 0
  int temp[] = {(create_signature(std::forward<KeyPairs>(keypairs)), 0)...};
  // use unused variable
  (void)temp;

  return batch;
}

inline auto makeSignature(const std::string &sign,
                          const std::string &public_key) {
  return std::make_pair(shared_model::crypto::Signed(sign),
                        shared_model::crypto::PublicKey(public_key));
}

inline auto makeTx(const shared_model::interface::types::CounterType &counter,
                   iroha::TimeType created_time = iroha::time::now(),
                   shared_model::crypto::Keypair keypair = makeKey(),
                   uint8_t quorum = 3) {
  return std::make_shared<shared_model::proto::Transaction>(
      shared_model::proto::TransactionBuilder()
          .createdTime(created_time)
          .creatorAccountId("user@test")
          .setAccountQuorum("user@test", counter)
          .quorum(quorum)
          .build()
          .signAndAddSignature(keypair)
          .finish());
}

inline auto makePeer(const std::string &address, const std::string &pub_key) {
  return std::make_shared<shared_model::proto::Peer>(
      shared_model::proto::PeerBuilder()
          .address(address)
          .pubkey(shared_model::crypto::PublicKey(pub_key))
          .build());
}

#endif  // IROHA_MST_TEST_HELPERS_HPP
