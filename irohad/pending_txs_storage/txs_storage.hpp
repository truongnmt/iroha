/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_TXS_STROAGE_HPP
#define IROHA_TXS_STROAGE_HPP

#include <map>
#include <memory>
#include <vector>
#include <unordered_map>

#include <rxcpp/rx.hpp>
#include "interfaces/common_objects/types.hpp"
#include "interfaces/transaction.hpp"
#include "logger/logger.hpp"

namespace iroha {

  class MstState;

  using AccountIdType = shared_model::interface::types::AccountIdType;
  using HashType = shared_model::interface::types::HashType;
  using SharedTransaction =
      std::shared_ptr<shared_model::interface::Transaction>;
  using SharedMstState = std::shared_ptr<MstState>;
  using StateObservable = rxcpp::observable<SharedMstState>;

  class PendingTransactionsStorage {
   public:
    /**
     *
     * @param updatedTransactions
     * @param preparedTransactions
     * @param expiredTransactions
     */
    PendingTransactionsStorage(StateObservable updatedTransactions,
                               StateObservable preparedTransactions,
                               StateObservable expiredTransactions);

    ~PendingTransactionsStorage();

    std::vector<SharedTransaction> getPendingTransactions(const AccountIdType &account_id);

   private:
    void updatedTxsHandler(const SharedMstState &state);
    void preparedTxsHandler(const SharedMstState &state);
    void expiredTxsHandler(const SharedMstState &state);

    rxcpp::composite_subscription updatedTxsSubscription;
    rxcpp::composite_subscription preparedTxsSubscription;
    rxcpp::composite_subscription expiredTxsSubscription;

    std::unordered_map<
        AccountIdType,
        std::unordered_map<HashType, SharedTransaction, HashType::Hasher>>
        storage;
  };

}  // namespace iroha

#endif  // IROHA_TXS_STROAGE_HPP
