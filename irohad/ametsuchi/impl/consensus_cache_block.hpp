/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_CONSENSUS_CACHE_BLOCK_H
#define IROHA_CONSENSUS_CACHE_BLOCK_H

#include "ametsuchi/consensus_cache.hpp"

#include "interfaces/iroha_internal/block_variant.hpp"

/**
 * Thread-safe implementation of consensus cache for storing block variants
 */
class ConsensusCacheBlock
    : public ConsensusCache<shared_model::interface::BlockVariant> {
 public:
  ConsensusCacheBlock();
  explicit ConsensusCacheBlock(DataPointer block);

  // we want only one unique consensus cache, so delete copy constructors
  ConsensusCacheBlock(const ConsensusCacheBlock &other) = delete;
  ConsensusCacheBlock &operator=(const ConsensusCacheBlock &other) = delete;

  void insert(DataPointer data) override;

  WrappedData get() const override;

  void release() override;

 private:
  DataPointer stored_block_;

  mutable std::mutex mutex_;
};

#endif  // IROHA_CONSENSUS_CACHE_BLOCK_H
