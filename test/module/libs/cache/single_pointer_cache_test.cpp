/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <thread>
#include <condition_variable>

#include <gtest/gtest.h>

#include "cache/single_pointer_cache.hpp"

using namespace iroha::cache;

class SinglePointerCacheTest : public ::testing::Test {
  using SinglePointerIntCache = SinglePointerCache<int>;

 protected:
  void SetUp() override {
    int_cache.release();
  }

  SinglePointerIntCache int_cache;
  const int default_int_value = 5;
};

/**
 * @given empty int cache
 * @when trying to get the value inside
 * @then cache will return nullptr
 */
TEST_F(SinglePointerCacheTest, GetWhenEmpty) {
  ASSERT_FALSE(int_cache.get());
}

/**
 * @given empty int cache
 * @when inserting some value into it @and trying to get it
 * @then cache will return the inserted value
 */
TEST_F(SinglePointerCacheTest, Insert) {
  int_cache.insert(std::make_shared<int>(default_int_value));
  ASSERT_EQ(*int_cache.get(), default_int_value);
}

/**
 * @given empty int cache
 * @when inserting some value into it @and releasing the cache @and trying to
 * get value inside
 * @then cache will return nullptr
 */
TEST_F(SinglePointerCacheTest, Release) {
  int_cache.insert(std::make_shared<int>(default_int_value));
  ASSERT_TRUE(int_cache.get());

  int_cache.release();
  ASSERT_FALSE(int_cache.get());
}

/**
 * @given empty int cache
 * @when first thread inserts value @and another tries to read it @and the first
 * removes it @and second read operation completes
 * @then the system could possibly crash, if it was not thread-safe - it would
 * try to give removed value to the second thread
 */
TEST_F(SinglePointerCacheTest, MultithreadedCache) {
  std::mutex mutex;
  std::condition_variable cv;
  bool reader_has_started = false;

  int_cache.insert(std::make_shared<int>(default_int_value));

  // we are going to wait until reader wakes up before releasing the cache,
  // otherwise there's no sense in the test
  std::thread reader{[this, &reader_has_started, &cv]() {
    reader_has_started = true;
    cv.notify_all();
    ASSERT_EQ(*int_cache.get(), default_int_value);
  }};
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, [&reader_has_started]() { return reader_has_started; });

  int_cache.release();
  reader.join();
}
