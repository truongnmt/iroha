/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <utility>

#include "ametsuchi/impl/consensus_cache_block.hpp"

ConsensusCacheBlock::ConsensusCacheBlock() = default;

ConsensusCacheBlock::ConsensusCacheBlock(ConsensusCache::DataPointer block)
    : stored_block_(std::move(block)) {}

void ConsensusCacheBlock::insert(ConsensusCache::DataPointer data) {
  std::lock_guard<std::mutex> lock(mutex_);

  stored_block_ = data;
}

ConsensusCache<shared_model::interface::BlockVariant>::DataPointer
ConsensusCacheBlock::get() const {
  std::lock_guard<std::mutex> lock(mutex_);

  return stored_block_ ? stored_block_ : nullptr;
}

void ConsensusCacheBlock::release() {
  std::lock_guard<std::mutex> lock(mutex_);

  stored_block_.reset();
}
