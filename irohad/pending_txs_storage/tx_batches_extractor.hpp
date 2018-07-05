/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_TX_BATCHES_EXTRACTOR_HPP
#define IROHA_TX_BATCHES_EXTRACTOR_HPP

#include <memory>
#include <vector>

#include "interfaces/common_objects/types.hpp"
#include "interfaces/transaction.hpp"

namespace iroha {

  /**
   * Allows to extract separate batches of transactions and single transactions
   * form the sequence of transactions
   */
  class TransactionBatchesExtractor {
   public:
    using SharedTx = std::shared_ptr<shared_model::interface::Transaction>;
    using HashType = shared_model::interface::types::HashType;

    TransactionBatchesExtractor(const std::vector<SharedTx> &transactions);

    /**
     * Returns amount of extracted batches
     */
    size_t size() const;

    /**
     * Retrieve batch by index
     * @param index 0 <= index < size()
     * @return transactions batch
     */
    std::vector<SharedTx> operator[](size_t index);

   private:
    HashType calculateBatchHash(std::vector<HashType> reduced_hashes);

    std::vector<std::vector<SharedTx>> batches;
  };

}  // namespace iroha

#endif  // IROHA_TX_BATCHES_EXTRACTOR_HPP
