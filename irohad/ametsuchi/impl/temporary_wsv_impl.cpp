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

#include "ametsuchi/impl/temporary_wsv_impl.hpp"

#include "ametsuchi/impl/postgres_wsv_command.hpp"
#include "ametsuchi/impl/postgres_wsv_query.hpp"
#include "amount/amount.hpp"

namespace iroha {
  namespace ametsuchi {
    TemporaryWsvImpl::TemporaryWsvImpl(
        std::unique_ptr<soci::session> sql)
        : sql_(std::move(sql)),
          wsv_(std::make_unique<PostgresWsvQuery>(*sql_)),
          executor_(std::make_unique<PostgresWsvCommand>(*sql_)),
          log_(logger::log("TemporaryWSV")) {
      auto query = std::make_shared<PostgresWsvQuery>(*sql_);
      auto command = std::make_shared<PostgresWsvCommand>(*sql_);
      command_executor_ = std::make_shared<CommandExecutor>(query, command);
      command_validator_ = std::make_shared<CommandValidator>(query);
      *sql_ << "BEGIN";
    }

    bool TemporaryWsvImpl::apply(
        const shared_model::interface::Transaction &tx,
        std::function<bool(const shared_model::interface::Transaction &,
                           WsvQuery &)> apply_function) {
      const auto &tx_creator = tx.creatorAccountId();
      command_executor_->setCreatorAccountId(tx_creator);
      command_validator_->setCreatorAccountId(tx_creator);
      auto execute_command = [this, &tx_creator](auto &command) {
        auto account = wsv_->getAccount(tx_creator).value();
        if (not boost::apply_visitor(*command_validator_, command.get())) {
          return false;
        }
        auto result = boost::apply_visitor(*command_executor_, command.get());
        return result.match([](expected::Value<void> &v) { return true; },
                            [this](expected::Error<ExecutionError> &e) {
                              log_->error(e.error.toString());
                              return false;
                            });
      };

      *sql_ << "SAVEPOINT savepoint2_";
      auto result =
          apply_function(tx, *wsv_)
          and std::all_of(
                  tx.commands().begin(), tx.commands().end(), execute_command);
      if (result) {
        *sql_ << "RELEASE SAVEPOINT savepoint2_";
      } else {
        *sql_ << "ROLLBACK TO SAVEPOINT savepoint2_";
      }
      return result;
    }

    TemporaryWsvImpl::~TemporaryWsvImpl() {
      *sql_ << "ROLLBACK";
    }
  }  // namespace ametsuchi
}  // namespace iroha
