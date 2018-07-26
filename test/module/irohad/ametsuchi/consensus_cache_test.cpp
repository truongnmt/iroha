/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include "ametsuchi/impl/consensus_cache_block.hpp"
#include "module/irohad/ametsuchi/ametsuchi_fixture.hpp"
#include "module/shared_model/builders/protobuf/test_block_builder.hpp"

class ConsensusCacheBlockTest : public ::testing::Test {
 protected:
  void SetUp() override {
    consensus_cache_block = std::make_shared<ConsensusCacheBlock>();
  }

  void TearDown() override {
    consensus_cache_block.reset();
  }

  std::shared_ptr<ConsensusCacheBlock> consensus_cache_block;
  const shared_model::interface::types::HeightType default_block_height = 5;
};

/**
 * @given up and running consensus cache for blocks
 * @when checking it for emptiness, inserting and getting the elements inside,
 * releasing cache and trying to get the element again
 * @then the cache works properly, returning empty optional when cache is empty
 * and elements, when it's not
 */
TEST_F(ConsensusCacheBlockTest, SingleThreadedCache) {
  ASSERT_FALSE(consensus_cache_block->get());

  auto block = std::make_shared<shared_model::proto::Block>(
      TestBlockBuilder().height(default_block_height).build());
  consensus_cache_block->insert(
      std::make_shared<shared_model::interface::BlockVariant>(
          std::move(block)));
  ASSERT_TRUE(consensus_cache_block->get());
  ASSERT_EQ(default_block_height, consensus_cache_block->get().get()->height());

  consensus_cache_block->release();
  ASSERT_FALSE(consensus_cache_block->get());
}

/**
 * @given up and running consensus cache for blocks
 * @when first thread inserts value @and another tries to read it @and the first
 * removes it @and second read operation completes
 * @then the system could possibly crash, if it was not thread-safe - it would
 * try to give removed value to the second thread
 */
TEST_F(ConsensusCacheBlockTest, MultithreadedCache) {
  std::mutex mutex;
  std::condition_variable cv;
  bool reader_has_started = false;

  auto block = std::make_shared<shared_model::proto::Block>(
      TestBlockBuilder().height(default_block_height).build());
  consensus_cache_block->insert(
      std::make_shared<shared_model::interface::BlockVariant>(
          std::move(block)));
  ASSERT_TRUE(consensus_cache_block->get());

  // we are going to wait until reader wakes up before releasing the cache,
  // otherwise there's no sense in the test
  std::thread reader{[this, &reader_has_started, &cv]() {
    reader_has_started = true;
    cv.notify_all();
    ASSERT_TRUE(consensus_cache_block->get().get());
  }};
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, [&reader_has_started]() { return reader_has_started; });

  consensus_cache_block->release();
  reader.join();

  ASSERT_FALSE(consensus_cache_block->get());
}
