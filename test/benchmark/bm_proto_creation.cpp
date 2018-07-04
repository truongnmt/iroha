/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * When passing through the pipeline, we need to form proposal
 * out of transactions and then make blocks out of it. This results in several
 * copies of underlying transport implementation, which can be visibly slow.
 *
 * The purpose of this benchmark is to keep track of performance costs related
 * to blocks and proposals copying/moving
 */

#include <benchmark/benchmark.h>
#include "backend/protobuf/block.hpp"
#include "builders/protobuf/block.hpp"
#include "builders/protobuf/transaction.hpp"
#include "datetime/time.hpp"
#include "module/shared_model/builders/protobuf/test_block_builder.hpp"
#include "module/shared_model/builders/protobuf/test_proposal_builder.hpp"
#include "module/shared_model/builders/protobuf/test_transaction_builder.hpp"
#include "module/shared_model/validators/validators.hpp"

class BlockBenchmark : public benchmark::Fixture {
 public:
  // Block cannot be copy-assigned, that's why the state is kept in a builder
  TestBlockBuilder complete_builder;

  void SetUp(benchmark::State &st) override {
    TestBlockBuilder builder;
    TestTransactionBuilder txbuilder;

    auto base_tx = txbuilder.createdTime(iroha::time::now()).quorum(1);

    for (int i = 0; i < 5; i++) {
      base_tx.transferAsset("player@one", "player@two", "coin", "", "5.00");
    }

    std::vector<shared_model::proto::Transaction> txs;

    for (int i = 0; i < 100; i++) {
      txs.push_back(base_tx.build());
    }

    complete_builder =
        builder.createdTime(iroha::time::now()).height(1).transactions(txs);
  }
};

class ProposalBenchmark : public benchmark::Fixture {
 public:
  // Block cannot be copy-assigned, that's why state is kept in a builder
  TestProposalBuilder complete_builder;

  void SetUp(benchmark::State &st) override {
    TestProposalBuilder builder;
    TestTransactionBuilder txbuilder;

    auto base_tx = txbuilder.createdTime(iroha::time::now()).quorum(1);

    for (int i = 0; i < 5; i++) {
      base_tx.transferAsset("player@one", "player@two", "coin", "", "5.00");
    }

    std::vector<shared_model::proto::Transaction> txs;

    for (int i = 0; i < 100; i++) {
      txs.push_back(base_tx.build());
    }

    complete_builder =
        builder.createdTime(iroha::time::now()).height(1).transactions(txs);
  }
};

BENCHMARK_F(BlockBenchmark, CopyTest)(benchmark::State &st) {
  auto block = complete_builder.build();

  while (st.KeepRunning()) {
    shared_model::proto::Block copy(block.getTransport());

    for (const auto &tx : copy.transactions()) {
      benchmark::DoNotOptimize(tx.commands());
    }
  }
}

BENCHMARK_F(BlockBenchmark, MoveTest)(benchmark::State &state) {
  auto block = complete_builder.build();

  while (state.KeepRunning()) {
    shared_model::proto::Block copy(std::move(block.getTransport()));

    for (const auto &tx : copy.transactions()) {
      benchmark::DoNotOptimize(tx.commands());
    }
  }
}

BENCHMARK_F(ProposalBenchmark, CopyTest)(benchmark::State &st) {
  auto proposal = complete_builder.build();

  while (st.KeepRunning()) {
    shared_model::proto::Proposal copy(proposal.getTransport());

    for (const auto &tx : copy.transactions()) {
      benchmark::DoNotOptimize(tx.commands());
    }
  }
}

BENCHMARK_F(ProposalBenchmark, MoveTest)(benchmark::State &state) {
  auto proposal = complete_builder.build();

  while (state.KeepRunning()) {
    shared_model::proto::Proposal copy(std::move(proposal.getTransport()));

    for (const auto &tx : copy.transactions()) {
      benchmark::DoNotOptimize(tx.commands());
    }
  }
}

BENCHMARK_MAIN();
