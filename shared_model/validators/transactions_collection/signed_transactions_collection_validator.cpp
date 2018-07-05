/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "validators/transactions_collection/signed_transactions_collection_validator.hpp"

#include <boost/format.hpp>
#include <boost/range/algorithm/copy.hpp>
#include "validators/field_validator.hpp"
#include "validators/transaction_validator.hpp"

namespace shared_model {
  namespace validation {

    template <typename TransactionValidator>
    Answer
    SignedTransactionsCollectionValidator<TransactionValidator>::validate(
        const interface::types::TransactionsForwardCollectionType &transactions)
        const {
      auto txs = transactions | boost::adaptors::transformed([](interface::Transaction &tx) {
        return std::shared_ptr<interface::Transaction>(clone(tx));
      });
//      std::vector<interface::Transaction> res;
//      boost::copy(txs, std::back_inserter(res));
//      return validate(txs);
    }

  template Answer SignedTransactionsCollectionValidator<
      TransactionValidator<FieldValidator,
                           CommandValidatorVisitor<FieldValidator>>>::
  validate(const interface::types::TransactionsForwardCollectionType
           &transactions) const;

    template <typename TransactionValidator>
    Answer
    SignedTransactionsCollectionValidator<TransactionValidator>::validate(
        const interface::types::SharedTxsCollectionType &transactions) const {
      ReasonsGroupType reason;
      reason.first = "Transaction list";
      for (const auto &tx : transactions) {
        auto answer =
            SignedTransactionsCollectionValidator::transaction_validator_
                .validate(*tx);
        if (answer.hasErrors()) {
          auto message =
              (boost::format("Tx %s : %s") % tx->hash().hex() % answer.reason())
                  .str();
          reason.second.push_back(message);
        }
      }

      Answer res;
      if (not reason.second.empty()) {
        res.addReason(std::move(reason));
      }
      return res;
    }

  template Answer SignedTransactionsCollectionValidator<
      TransactionValidator<FieldValidator,
                           CommandValidatorVisitor<FieldValidator>>>::
  validate(const interface::types::SharedTxsCollectionType &transactions)
  const;

  }  // namespace validation
}  // namespace shared_model
