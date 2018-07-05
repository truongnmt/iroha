/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sstream>
#include <string>
#include <unordered_map>

#include "cryptography/hash.hpp"
#include "interfaces/iroha_internal/batch_meta.hpp"
#include "pending_txs_storage/tx_batches_extractor.hpp"

namespace iroha {

  TransactionBatchesExtractor::TransactionBatchesExtractor(
      const std::vector<SharedTx> &transactions) {
    std::unordered_map<HashType, std::vector<SharedTx>, HashType::Hasher>
        extracted_batches;
    for (const auto &tx : transactions) {
      if (auto meta = tx->batch_meta()) {
        auto hashes = meta.get()->transactionHashes();
        auto batch_hash = calculateBatchHash(hashes);
        extracted_batches[batch_hash].push_back(tx);
      } else {
        batches.push_back(std::vector<SharedTx>{tx});
      }
    }
    for (auto it : extracted_batches) {
      batches.push_back(it.second);
    }
  }

  size_t TransactionBatchesExtractor::size() const {
    return batches.size();
  }

  std::vector<TransactionBatchesExtractor::SharedTx>
      TransactionBatchesExtractor::operator[](size_t index) {
    return batches[index];
  }

  TransactionBatchesExtractor::HashType
  TransactionBatchesExtractor::calculateBatchHash(
      std::vector<HashType> reduced_hashes) {
    std::stringstream concatenated_hashes_stream;
    for (const auto &hash : reduced_hashes) {
      concatenated_hashes_stream << hash.hex();
    }
    return shared_model::crypto::Hash::fromHexString(
        concatenated_hashes_stream.str());
  }

}  // namespace iroha
