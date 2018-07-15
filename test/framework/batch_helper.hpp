/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_BATCH_HELPER_HPP
#define IROHA_BATCH_HELPER_HPP

#include "module/shared_model/builders/protobuf/test_transaction_builder.hpp"

namespace framework {
  namespace batch {

    /**
     * Creates transaction builder with set creator
     * @return prepared transaction builder
     */
    auto prepareTransactionBuilder(
        const std::string &creator,
        const size_t &created_time = iroha::time::now()) {
      return TestTransactionBuilder()
          .setAccountQuorum(creator, 1)
          .creatorAccountId(creator)
          .createdTime(created_time)
          .quorum(1);
    }

    auto createUnsignedBatch(
        boost::any_range<
            std::pair<shared_model::interface::types::BatchType, std::string>,
            boost::forward_traversal_tag> btype_creator_pairs) {
      auto now = iroha::time::now();

      std::vector<shared_model::interface::types::HashType> reduced_hashes;
      for (const auto &btype_creator : btype_creator_pairs) {
        auto tx = prepareTransactionBuilder(btype_creator.second, now).build();
        reduced_hashes.push_back(tx.reducedHash());
      }

      shared_model::interface::types::SharedTxsCollectionType txs;
      std::for_each(btype_creator_pairs.begin(),
                    btype_creator_pairs.end(),
                    [&now, &txs, &reduced_hashes](const auto &btype_creator) {
                      txs.emplace_back(clone(
                          prepareTransactionBuilder(btype_creator.second, now)
                              .batchMeta(btype_creator.first, reduced_hashes)
                              .build()));
                    });
      return txs;
    }

    /**
     * Creates atomic batch from provided creator accounts
     * @param creators vector of creator account ids
     * @return atomic batch of the same size as the size of creator account ids
     */
    auto createUnsignedBatch(
        shared_model::interface::types::BatchType batch_type,
        std::vector<std::string> creators) {
      std::vector<shared_model::interface::types::HashType> reduced_hashes;

      return createUnsignedBatch(
          creators
          | boost::adaptors::transformed([&batch_type](const auto &creator) {
              return std::make_pair(batch_type, creator);
            }));
    }

  }  // namespace batch
}  // namespace framework

#endif  // IROHA_BATCH_HELPER_HPP
