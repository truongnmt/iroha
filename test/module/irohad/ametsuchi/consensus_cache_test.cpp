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
    consensus_cache_block =
        std::make_shared<ConsensusCacheBlock>();
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

TEST_F(ConsensusCacheBlockTest, MultithreadedCache) {}
