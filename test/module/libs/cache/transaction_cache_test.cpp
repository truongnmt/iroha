/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <vector>

#include "cache/collection_set.hpp"

using namespace iroha::set;

/**
 * @given empty set
 * @when  check that empty set doesn't contain elements
 * AND insert some collection
 * @then  check that elements are appeared
 */
TEST(TransactionCacheTest, insert) {
  auto number_of_calls = 0;
  CollectionSet<int> set;
  set.foreach ([&number_of_calls](const auto &val) { number_of_calls++; });
  ASSERT_EQ(0, number_of_calls);

  set.insertValues(std::vector<int>({1, 2}));

  set.foreach ([&number_of_calls](const auto &val) { number_of_calls++; });
  ASSERT_EQ(2, number_of_calls);
}

/**
 * @given empty set
 * @when insert some collection
 * AND insert duplicated
 * @then  check that duplicates are not appeared
 */
TEST(TransactionCacheTest, insertDuplicates) {
  auto number_of_calls = 0;
  CollectionSet<int> set;

  set.insertValues(std::vector<int>({1, 2}));
  set.insertValues(std::vector<int>({1, 3}));

  set.foreach ([&number_of_calls](const auto &val) { number_of_calls++; });
  ASSERT_EQ(3, number_of_calls);
}

/**
 * @given empty set
 * @when insert some collection
 * AND remove another collection with same and different elements
 * @then  check that duplicates and removed elements are not appeared
 */
TEST(TransactionCacheTest, remove) {
  auto number_of_calls = 0;
  CollectionSet<int> set;

  set.insertValues(std::vector<int>({1, 2, 3}));
  set.removeValues(std::vector<int>({1, 3, 4}));
  set.foreach ([&number_of_calls](const auto &val) { number_of_calls++; });
  ASSERT_EQ(1, number_of_calls);
}
