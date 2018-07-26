/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_CONSENSUS_CACHE_H
#define IROHA_CONSENSUS_CACHE_H

#include <memory>

/**
 * Stores pointer to some data
 */
template <typename DataType,
          template <typename> class PointerType = std::shared_ptr>
class PointerCache {
 public:
  using DataPointer = PointerType<DataType>;

  /**
   * Insert data to the cache
   * @param pointer to the data to be inserted
   */
  virtual void insert(DataPointer data) = 0;

  /**
   * Get data from the cache
   * @return pointer to the stored data
   */
  virtual DataPointer get() const = 0;

  /**
   * Delete data inside the cache
   */
  virtual void release() = 0;

  virtual ~PointerCache() = default;
};

#endif  // IROHA_CONSENSUS_CACHE_H
