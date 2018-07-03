/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
 #define BOOST_NO_RTTI
#include <benchmark/benchmark.h>
#include "backend/protobuf/block.hpp"
#include "builders/protobuf/block.hpp"
#include "builders/protobuf/transaction.hpp"
#include "datetime/time.hpp"
#include "../module/shared_model/validators/validators.hpp"

using TestTransactionBuilder = shared_model::proto::TemplateTransactionBuilder<
    (1 << shared_model::proto::TemplateTransactionBuilder<>::total) - 1,
    shared_model::validation::AlwaysValidValidator,
    shared_model::proto::Transaction>;

using TestBlockBuilder = shared_model::proto::TemplateBlockBuilder<
    (1 << shared_model::proto::TemplateBlockBuilder<>::total) - 1,
    shared_model::validation::AlwaysValidValidator,
    shared_model::proto::Block>;

static void BM_BlockCopy(benchmark::State &state) {

  TestBlockBuilder builder;
  TestTransactionBuilder txbuilder;

  auto base_tx = txbuilder
      .createdTime(iroha::time::now())
      .quorum(1);

  for (int i = 0; i < 5; i++) {
    base_tx.transferAsset("player@one", "player@two", "coin", "", "5.00");
  }


  std::vector<shared_model::proto::Transaction> txs;

  for (int i = 0; i < 100; i++) {
    txs.push_back(base_tx.build());
  }
  auto block = builder.createdTime(iroha::time::now())
      .height(1)
      .transactions(txs).build();

  // define main benchmark loop
  while (state.KeepRunning()) {
    shared_model::proto::Block copy(block.getTransport());

    for (const auto &tx : copy.transactions()) {
      benchmark::DoNotOptimize(tx.commands());
    }
    // define the code to be tested
  }
}

static void BM_BlockMove(benchmark::State &state) {

  TestBlockBuilder builder;
  TestTransactionBuilder txbuilder;

  auto base_tx = txbuilder
      .createdTime(iroha::time::now())
      .quorum(1);

  for (int i = 0; i < 5; i++) {
    base_tx.transferAsset("player@one", "player@two", "coin", "", "5.00");
  }


  std::vector<shared_model::proto::Transaction> txs;

  for (int i = 0; i < 100; i++) {
    txs.push_back(base_tx.build());
  }
  auto block = builder.createdTime(iroha::time::now())
      .height(1)
      .transactions(txs).build();

  // define main benchmark loop
  while (state.KeepRunning()) {
    shared_model::proto::Block copy(std::move(block.getTransport()));

    for (const auto &tx : copy.transactions()) {
      benchmark::DoNotOptimize(tx.commands());
    }
    // define the code to be tested
  }
}



BENCHMARK(BM_BlockCopy)->Repetitions(100)->ReportAggregatesOnly(true);
BENCHMARK(BM_BlockMove)->Repetitions(100)->ReportAggregatesOnly(true);

BENCHMARK_MAIN();

