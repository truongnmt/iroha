/**
 * Copyright Soramitsu Co., Ltd. 2017 All Rights Reserved.
 * http://soramitsu.co.jp
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <utility>
#include "backend/protobuf/block.hpp"
#include "backend/protobuf/empty_block.hpp"
#include "interfaces/iroha_internal/block_variant.hpp"

#include "ametsuchi/mutable_storage.hpp"
#include "synchronizer/impl/synchronizer_impl.hpp"

namespace iroha {
  namespace synchronizer {

    SynchronizerImpl::SynchronizerImpl(
        std::shared_ptr<network::ConsensusGate> consensus_gate,
        std::shared_ptr<validation::ChainValidator> validator,
        std::shared_ptr<ametsuchi::MutableFactory> mutableFactory,
        std::shared_ptr<network::BlockLoader> blockLoader)
        : validator_(std::move(validator)),
          mutableFactory_(std::move(mutableFactory)),
          block_loader_(std::move(blockLoader)),
          log_(logger::log("synchronizer")) {
      consensus_gate->on_commit().subscribe(
          subscription_,
          [&](const shared_model::interface::BlockVariant &block_variant) {
            this->process_commit(block_variant);
          });
    }

    SynchronizerImpl::~SynchronizerImpl() {
      subscription_.unsubscribe();
    }

    namespace {
      /**
       * Lambda always returning true specially for applying blocks to storage
       */
      auto trueStorageApplyPredicate = [](const auto &, auto &, const auto &) {
        return true;
      };

      /**
       * Creates a temporary storage out of the provided factory
       * @param mutable_factory to create storage
       * @param log to tell about errors
       * @return pointer to created storage
       */
      std::unique_ptr<ametsuchi::MutableStorage> createTemporaryStorage(
          std::shared_ptr<ametsuchi::MutableFactory> mutable_factory,
          logger::Logger log) {
        return mutable_factory->createMutableStorage().match(
            [](expected::Value<std::unique_ptr<ametsuchi::MutableStorage>>
                   &created_storage) {
              return std::move(created_storage.value);
            },
            [&log](expected::Error<std::string> &error) {
              log->error("could not create mutable storage: {}", error.error);
              return std::unique_ptr<ametsuchi::MutableStorage>{};
            });
      }

      /**
       * Process block, which can be applied to current storage directly:
       *   - apply non-empty block and commit result to Ametsuchi
       *     @or
       *   - don't apply empty block
       * In both cases notify the subscriber about commit
       * @param committed_block_variant to be applied
       * @param notifier to notify the pcs about commit
       * @param mutable_factory to create storage for block application
       * @param log to tell about errors
       */
      void processApplicableBlock(
          const shared_model::interface::BlockVariant &committed_block_variant,
          const rxcpp::subjects::subject<Commit> &notifier,
          std::shared_ptr<ametsuchi::MutableFactory> mutable_factory,
          logger::Logger log) {
        iroha::visit_in_place(
            committed_block_variant,
            [&](std::shared_ptr<shared_model::interface::Block> block_ptr) {
              auto applyStorage = createTemporaryStorage(mutable_factory, log);
              if (not applyStorage) {
                return;
              }
              applyStorage->apply(*block_ptr, trueStorageApplyPredicate);
              mutable_factory->commit(std::move(applyStorage));

              notifier.get_subscriber().on_next(
                  rxcpp::observable<>::just(block_ptr));
            },
            [&](std::shared_ptr<shared_model::interface::EmptyBlock>
                    empty_block_ptr) {
              notifier.get_subscriber().on_next(
                  rxcpp::observable<>::empty<
                      std::shared_ptr<shared_model::interface::Block>>());
            });
      }

      /**
       * Process block, which cannot be applied to current storage directly:
       *   - try to download missing blocks from other peers (don't stop, if
       *     they cannot provide blocks at that moment)
       *   - apply the chain on top of existing storage and commit result
       * Committed block variant is not applied, because it's either empty or
       * already exists in downloaded chain
       * @param committed_block_variant to be processed
       * @param notifier to notify the pcs about commit
       * @param mutable_factory to commit results
       * @param storage to validate and apply chain
       * @param block_loader to download the blocks
       * @param validator to validate chain
       */
      void processUnapplicableBlock(
          const shared_model::interface::BlockVariant &committed_block_variant,
          const rxcpp::subjects::subject<Commit> &notifier,
          std::shared_ptr<ametsuchi::MutableFactory> mutable_factory,
          std::unique_ptr<ametsuchi::MutableStorage> storage,
          std::shared_ptr<network::BlockLoader> block_loader,
          std::shared_ptr<validation::ChainValidator> validator) {
        const auto committed_block_is_empty = iroha::visit_in_place(
            committed_block_variant,
            [](std::shared_ptr<shared_model::interface::Block>) {
              return false;
            },
            [](std::shared_ptr<shared_model::interface::EmptyBlock>) {
              return true;
            });
        auto sync_complete = false;
        while (not sync_complete) {
          for (const auto &signature : committed_block_variant.signatures()) {
            std::vector<std::shared_ptr<shared_model::interface::Block>> blocks;
            block_loader
                ->retrieveBlocks(
                    shared_model::crypto::PublicKey(signature.publicKey()))
                .as_blocking()
                .subscribe([&blocks](auto block) { blocks.push_back(block); });
            auto chain = rxcpp::observable<>::iterate(
                blocks, rxcpp::identity_immediate());
            // if committed block is not empty, it will be on top of downloaded
            // chain; otherwise, it'll contain hash of top of that chain
            auto chain_ends_with_right_block = committed_block_is_empty
                ? blocks.back()->hash() == committed_block_variant.prevHash()
                : blocks.back()->hash() == committed_block_variant.hash();

            if (validator->validateChain(chain, *storage)
                and chain_ends_with_right_block) {
              // peer sent valid chain
              notifier.get_subscriber().on_next(chain);

              for (const auto &block : blocks) {
                storage->apply(*block, trueStorageApplyPredicate);
              }
              mutable_factory->commit(std::move(storage));

              // we are finished
              sync_complete = true;
              break;
            }
          }
        }
      }
    }  // namespace

    void SynchronizerImpl::process_commit(
        const shared_model::interface::BlockVariant &committed_block_variant) {
      log_->info("processing commit");
      auto storage = createTemporaryStorage(mutableFactory_, log_);
      if (not storage) {
        return;
      }

      if (validator_->validateBlock(committed_block_variant, *storage)) {
        processApplicableBlock(
            committed_block_variant, notifier_, mutableFactory_, log_);
      } else {
        processUnapplicableBlock(committed_block_variant,
                                 notifier_,
                                 mutableFactory_,
                                 std::move(storage),
                                 block_loader_,
                                 validator_);
      }
    }

    rxcpp::observable<Commit> SynchronizerImpl::on_commit_chain() {
      return notifier_.get_observable();
    }

  }  // namespace synchronizer
}  // namespace iroha
