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

#include "network/impl/block_loader_service.hpp"
#include <validators/block_validator.hpp>
#include "backend/protobuf/block.hpp"
#include "builders/protobuf/block_variant_transport_builder.hpp"
#include "validators/default_validator.hpp"

using namespace iroha;
using namespace iroha::ametsuchi;
using namespace iroha::network;

BlockLoaderService::BlockLoaderService(
    std::shared_ptr<BlockQuery> storage,
    std::shared_ptr<iroha::consensus::ConsensusBlockCache> block_cache)
    : storage_(std::move(storage)),
      block_cache_(std::move(block_cache)),
      log_(logger::log("BlockLoaderService")) {}

grpc::Status BlockLoaderService::retrieveBlocks(
    ::grpc::ServerContext *context,
    const proto::BlocksRequest *request,
    ::grpc::ServerWriter<::iroha::protocol::Block> *writer) {
  storage_->getBlocksFrom(request->height())
      .map([](auto block) {
        return std::dynamic_pointer_cast<shared_model::proto::Block>(block)
            ->getTransport();
      })
      .as_blocking()
      .subscribe([writer](auto block) { writer->Write(block); });
  return grpc::Status::OK;
}

grpc::Status BlockLoaderService::retrieveBlock(
    ::grpc::ServerContext *context,
    const proto::BlockRequest *request,
    protocol::Block *response) {
  const auto hash = shared_model::crypto::Hash(request->hash());
  if (hash.size() == 0) {
    log_->error("Bad hash in request");
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "Bad hash provided");
  }

  // required block must be in the cache
  auto block_from_cache = block_cache_->get();
  if (not block_from_cache) {
    log_->info("Requested to retrieve a block from an empty cache");
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "Cache is empty");
  }
  if (block_from_cache->hash() != hash) {
    log_->info(
        "Requested to retrieve a block with hash other than the one in cache");
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "Block not found");
  }

  auto transport_block = iroha::visit_in_place(
      *block_from_cache,
      [](auto block) {
        return std::static_pointer_cast<shared_model::proto::Block>(block)->getTransport();
      },
      [](std::shared_ptr<shared_model::interface::EmptyBlock> empty_block) {
        return std::static_pointer_cast<shared_model::proto::EmptyBlock>(
                   empty_block)
            ->getTransport();
      });
  response->CopyFrom(transport_block);
  return grpc::Status::OK;
}
