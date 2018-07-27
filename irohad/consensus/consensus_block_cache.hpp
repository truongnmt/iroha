/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_CONSENSUS_CACHE_BLOCK_HPP
#define IROHA_CONSENSUS_CACHE_BLOCK_HPP

#include "interfaces/iroha_internal/block_variant.hpp"
#include "cache/single_pointer_cache.hpp"

namespace iroha {
  namespace consensus {

    /**
     * Type to represent consensus cache for a single block
     */
    using ConsensusBlockCache =
        iroha::cache::SinglePointerCache<shared_model::interface::BlockVariant>;

  }  // namespace consensus
}  // namespace iroha

#endif  // IROHA_CONSENSUS_CACHE_BLOCK_HPP
