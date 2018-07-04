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
#include "module/shared_model/validators/validators.hpp"
#include "module/shared_model/builders/protobuf/test_transaction_builder.hpp"
#include "module/shared_model/builders/protobuf/test_block_builder.hpp"
#include "module/shared_model/builders/protobuf/test_proposal_builder.hpp"

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

  while (state.KeepRunning()) {
    shared_model::proto::Block copy(block.getTransport());

    for (const auto &tx : copy.transactions()) {
      benchmark::DoNotOptimize(tx.commands());
    }
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

  while (state.KeepRunning()) {
    shared_model::proto::Block copy(std::move(block.getTransport()));

    for (const auto &tx : copy.transactions()) {
      benchmark::DoNotOptimize(tx.commands());
    }
  }
}

static void BM_ProposalCopy(benchmark::State &state) {

  TestProposalBuilder builder;
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
  auto proposal = builder.createdTime(iroha::time::now())
      .height(1)
      .transactions(txs).build();

  while (state.KeepRunning()) {
    shared_model::proto::Proposal copy(proposal.getTransport());

    for (const auto &tx : copy.transactions()) {
      benchmark::DoNotOptimize(tx.commands());
    }
  }
}

static void BM_ProposalMove(benchmark::State &state) {

  TestProposalBuilder builder;
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
  auto proposal = builder.createdTime(iroha::time::now())
      .height(1)
      .transactions(txs).build();

  while (state.KeepRunning()) {
    shared_model::proto::Proposal copy(std::move(proposal.getTransport()));

    for (const auto &tx : copy.transactions()) {
      benchmark::DoNotOptimize(tx.commands());
    }
  }
}


BENCHMARK(BM_BlockCopy);
BENCHMARK(BM_BlockMove);
BENCHMARK(BM_ProposalCopy);
BENCHMARK(BM_ProposalMove);

BENCHMARK_MAIN();

