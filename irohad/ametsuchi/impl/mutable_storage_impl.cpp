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

#include "ametsuchi/impl/mutable_storage_impl.hpp"

#include <boost/variant/apply_visitor.hpp>

#include "ametsuchi/impl/postgres_block_index.hpp"
#include "ametsuchi/impl/postgres_wsv_command.hpp"
#include "ametsuchi/impl/postgres_wsv_query.hpp"
#include "ametsuchi/wsv_command.hpp"
#include "model/sha3_hash.hpp"
#include "storage_impl.hpp"

namespace iroha {
  namespace ametsuchi {
    MutableStorageImpl::MutableStorageImpl(
        shared_model::interface::types::HashType top_hash,
        std::unique_ptr<pqxx::lazyconnection> connection,
        std::unique_ptr<pqxx::nontransaction> transaction,
        std::unique_ptr<soci::session> sql)
        : sql_(std::move(sql)),
          top_hash_(top_hash),
          connection_(std::move(connection)),
          transaction_(std::move(transaction)),
          wsv_(std::make_unique<PostgresWsvQuery>(*sql_)),
          executor_(std::make_unique<PostgresWsvCommand>(*sql_)),
          block_index_(std::make_unique<PostgresBlockIndex>(*transaction_)),
          committed(false),
          log_(logger::log("MutableStorage")) {
      auto query = std::make_shared<PostgresWsvQuery>(*sql_);
      auto command = std::make_shared<PostgresWsvCommand>(*sql_);
      command_executor_ =
          std::make_shared<CommandExecutor>(CommandExecutor(query, command));
    }

    bool MutableStorageImpl::apply(
        const shared_model::interface::Block &block,
        std::function<bool(const shared_model::interface::Block &,
                           WsvQuery &,
                           const shared_model::interface::types::HashType &)>
            function) {
      
      bool commited = false;
      while (!StorageImpl::prepared_txs.empty()) {
        try {
          *sql_ << "COMMIT PREPARED '" + (StorageImpl::prepared_txs.back()) + "'";
          log_->info("Committing transaction {}", (StorageImpl::prepared_txs.back()));
          StorageImpl::prepared_txs.pop_back();
          commited = true;
        } catch (...) {
        }
      }

      if (commited) {
        block_store_.insert(std::make_pair(block.height(), clone(block)));
        block_index_->index(block);

        top_hash_ = block.hash();
        return true;
      }

      *sql_ << "BEGIN";

      auto execute_transaction = [this](auto &transaction) {
        command_executor_->setCreatorAccountId(transaction.creatorAccountId());
//        log_->info("executing transaction");
        auto execute_command = [this](auto &command) {
//          log_->info("executing command");
          auto result = boost::apply_visitor(*command_executor_, command.get());
          return result.match([](expected::Value<void> &v) { return true; },
                              [&](expected::Error<ExecutionError> &e) {
                                log_->error(e.error.toString());
                                return false;
                              });
        };
        return std::all_of(transaction.commands().begin(),
                           transaction.commands().end(),
                           execute_command);
      };

      *sql_ << "SAVEPOINT savepoint2_";
      auto result = function(block, *wsv_, top_hash_)
          and std::all_of(block.transactions().begin(),
                          block.transactions().end(),
                          execute_transaction);

      if (result) {
        block_store_.insert(std::make_pair(block.height(), clone(block)));
        block_index_->index(block);

        top_hash_ = block.hash();
        *sql_ << "RELEASE SAVEPOINT savepoint2_";
      } else {
        *sql_ << "ROLLBACK TO SAVEPOINT savepoint2_";
      }

      return result;
    }

    MutableStorageImpl::~MutableStorageImpl() {
      if (not committed) {
//        *sql_ << "ROLLBACK";
      }
    }
  }  // namespace ametsuchi
}  // namespace iroha
