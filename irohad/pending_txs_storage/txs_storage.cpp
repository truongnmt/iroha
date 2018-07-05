/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pending_txs_storage/txs_storage.hpp"
#include "multi_sig_transactions/state/mst_state.hpp"

namespace iroha {

  PendingTransactionsStorage::PendingTransactionsStorage(
      iroha::StateObservable updatedTransactions,
      iroha::StateObservable preparedTransactions,
      iroha::StateObservable expiredTransactions) {
    updatedTxsSubscription =
        updatedTransactions.subscribe([this](const SharedMstState &update) {
          this->updatedTxsHandler(update);
        });
    preparedTxsSubscription =
        preparedTransactions.subscribe([this](const SharedMstState &update) {
          this->preparedTxsHandler(update);
        });
    expiredTxsSubscription =
        expiredTransactions.subscribe([this](const SharedMstState &update) {
          this->expiredTxsHandler(update);
        });
  }

  std::vector<SharedTransaction>
  PendingTransactionsStorage::getPendingTransactions(
      const iroha::AccountIdType &account_id) {
    std::vector<SharedTransaction> x;
    // TODO
    return x;
  }

  void PendingTransactionsStorage::updatedTxsHandler(
      const SharedMstState &state) {
    auto txs = state->getTransactions(); //vector<shared_ptr<Transaction>>
  }

  void PendingTransactionsStorage::preparedTxsHandler(
      const iroha::SharedMstState &state) {}

  void PendingTransactionsStorage::expiredTxsHandler(
      const iroha::SharedMstState &state) {}

  PendingTransactionsStorage::~PendingTransactionsStorage() {
    updatedTxsSubscription.unsubscribe();
    preparedTxsSubscription.unsubscribe();
    expiredTxsSubscription.unsubscribe();
  }

}  // namespace iroha
