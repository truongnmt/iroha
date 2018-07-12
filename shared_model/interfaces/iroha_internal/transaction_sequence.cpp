/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "interfaces/iroha_internal/transaction_sequence.hpp"
#include "validators/field_validator.hpp"
#include "validators/transaction_validator.hpp"
#include "validators/transactions_collection/any_order_validator.hpp"
#include "validators/transactions_collection/batch_order_validator.hpp"

namespace shared_model {
  namespace interface {

    template <typename TransactionValidator, typename OrderValidator>
    iroha::expected::Result<TransactionSequence, std::string>
    TransactionSequence::createTransactionSequence(
        const types::SharedTxsCollectionType &transactions,
        const validation::TransactionsCollectionValidator<TransactionValidator,
                                                          OrderValidator>
            &validator) {
      std::unordered_map<std::string, std::vector<std::shared_ptr<Transaction>>>
          extracted_batches;
      std::vector<TransactionBatch> batches;

      auto calculateBatchHash = [](const auto &reduced_hashes) {
        std::stringstream concatenated_hashes_stream;
        for (const auto &hash : reduced_hashes) {
          concatenated_hashes_stream << hash.hex();
        }
        return concatenated_hashes_stream.str();
      };

      auto transaction_validator = validator.getTransactionValidator();

      auto insert_batch =
          [&batches](const iroha::expected::Value<TransactionBatch> &value) {
            batches.push_back(value.value);
          };

      validation::Answer result;
      for (const auto &tx : transactions) {
        if (auto meta = tx->batchMeta()) {
          auto hashes = meta.get()->transactionHashes();
          auto batch_hash = calculateBatchHash(hashes);
          extracted_batches[batch_hash].push_back(tx);
        } else {
          TransactionBatch::createTransactionBatch(tx, transaction_validator)
              .match(insert_batch, [&tx, &result](const auto &err) {
                result.addReason(
                    std::make_pair(std::string("Transaction reduced hash: ")
                                       + tx->reducedHash().hex(),
                                   std::vector<std::string>{err.error}));
              });
        }
      }

      for (const auto &it : extracted_batches) {
        TransactionBatch::createTransactionBatch(it.second, validator)
            .match(insert_batch, [&it, &result](const auto &err) {
              result.addReason(std::make_pair(
                  it.first, std::vector<std::string>{err.error}));
            });
      }

      if (result.hasErrors()) {
        return iroha::expected::makeError(result.reason());
      }

      return iroha::expected::makeValue(TransactionSequence(batches));
    }

    template iroha::expected::Result<TransactionSequence, std::string>
    TransactionSequence::createTransactionSequence(
        const types::SharedTxsCollectionType &transactions,
        const validation::TransactionsCollectionValidator<
            validation::TransactionValidator<
                validation::FieldValidator,
                validation::CommandValidatorVisitor<
                    validation::FieldValidator>>,
            validation::AnyOrderValidator> &validator);

    template iroha::expected::Result<TransactionSequence, std::string>
    TransactionSequence::createTransactionSequence(
        const types::SharedTxsCollectionType &transactions,
        const validation::TransactionsCollectionValidator<
            validation::TransactionValidator<
                validation::FieldValidator,
                validation::CommandValidatorVisitor<
                    validation::FieldValidator>>,
            validation::BatchOrderValidator> &validator);

    types::BatchesCollectionType TransactionSequence::batches() const {
      return batches_;
    }

    TransactionSequence::TransactionSequence(
        const types::BatchesCollectionType &batches)
        : batches_(batches) {}

    std::string TransactionSequence::calculateBatchHash(
        std::vector<types::HashType> reduced_hashes) const {
      std::stringstream concatenated_hashes_stream;
      for (const auto &hash : reduced_hashes) {
        concatenated_hashes_stream << hash.hex();
      }
      return concatenated_hashes_stream.str();
    }

  }  // namespace interface
}  // namespace shared_model
