/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_TRANSACTION_BATCH_HPP
#define IROHA_TRANSACTION_BATCH_HPP

#include "common/result.hpp"
#include "interfaces/common_objects/transaction_sequence_common.hpp"
#include "validators/transactions_collection/transactions_collection_validator.hpp"

namespace shared_model {
  namespace interface {

    class TransactionBatch {
     public:
      TransactionBatch() = delete;

      template <typename TransactionValidator, typename OrderValidator>
      static iroha::expected::Result<TransactionBatch, std::string>
      createTransactionBatch(const types::SharedTxsCollectionType &transactions,
                             const validation::TransactionsCollectionValidator<
                                 TransactionValidator,
                                 OrderValidator> &validator) {
        auto answer = validator.validatePointers(transactions);
        if (answer.hasErrors()) {
          return iroha::expected::makeError(answer.reason());
        }
        return iroha::expected::makeValue(TransactionBatch(transactions));
      }

      template <typename TransactionValidator>
      static iroha::expected::Result<TransactionBatch, std::string>
      createTransactionBatch(std::shared_ptr<Transaction> transaction,
                             const TransactionValidator &transaction_validator =
                                 TransactionValidator()) {
        auto answer = transaction_validator.validate(*transaction);
        if (answer.hasErrors()) {
          return iroha::expected::makeError(answer.reason());
        }
        return iroha::expected::makeValue(
            TransactionBatch(types::SharedTxsCollectionType{transaction}));
      };

      types::SharedTxsCollectionType transactions() const;

     private:
      explicit TransactionBatch(
          const types::SharedTxsCollectionType &transactions)
          : transactions_(transactions) {}

      types::SharedTxsCollectionType transactions_;
    };
  }  // namespace interface
}  // namespace shared_model

#endif  // IROHA_TRANSACTION_BATCH_HPP
