/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ametsuchi/impl/postgres_query_executor.hpp"

#include <boost-tuple.h>
#include <soci/boost-tuple.h>
#include <soci/postgresql/soci-postgresql.h>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/optional/optional_io.hpp>
#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/algorithm/transform.hpp>

#include "ametsuchi/impl/soci_utils.hpp"
#include "common/types.hpp"
#include "converters/protobuf/json_proto_converter.hpp"
#include "interfaces/queries/blocks_query.hpp"

using namespace shared_model::interface::permissions;

namespace {

  using namespace iroha;

  /**
   * Generates a query response that contains an error response
   * @tparam T The error to return
   * @param query_hash Query hash
   * @return builder for QueryResponse
   */
  template <class T>
  shared_model::proto::TemplateQueryResponseBuilder<1> buildError() {
    return shared_model::proto::TemplateQueryResponseBuilder<0>()
        .errorQueryResponse<T>();
  }

  /**
   * Generates a query response that contains a concrete error (StatefulFailed)
   * @param query_hash Query hash
   * @return builder for QueryResponse
   */
  shared_model::proto::TemplateQueryResponseBuilder<1> statefulFailed() {
    return buildError<shared_model::interface::StatefulFailedErrorResponse>();
  }

  /**
   * Transforms result to optional
   * value -> optional<value>
   * error -> nullopt
   * @tparam T type of object inside
   * @param result BuilderResult
   * @return optional<T>
   */
  template <typename T>
  boost::optional<std::shared_ptr<T>> fromResult(
      shared_model::interface::CommonObjectsFactory::FactoryResult<
          std::unique_ptr<T>> &&result) {
    return result.match(
        [](expected::Value<std::unique_ptr<T>> &v) {
          return boost::make_optional(std::shared_ptr<T>(std::move(v.value)));
        },
        [](expected::Error<std::string>)
            -> boost::optional<std::shared_ptr<T>> { return boost::none; });
  }

  boost::optional<std::string> getDomainFromName(
      const std::string &account_id) {
    std::vector<std::string> res;
    boost::split(res, account_id, boost::is_any_of("@"));
    return res.size() > 1 ? boost::make_optional(res.at(1)) : boost::none;
  }

  std::string checkAccountRolePermission(
      shared_model::interface::permissions::Role permission,
      const std::string &account_alias = "role_account_id") {
    const auto perm_str =
        shared_model::interface::RolePermissionSet({permission}).toBitstring();
    const auto bits = shared_model::interface::RolePermissionSet::size();
    std::string query = (boost::format(R"(
          SELECT (COALESCE(bit_or(rp.permission), '0'::bit(%1%))
          & '%2%') = '%2%' AS perm FROM role_has_permissions AS rp
              JOIN account_has_roles AS ar on ar.role_id = rp.role_id
              WHERE ar.account_id = :%3%)")
                         % bits % perm_str % account_alias)
                            .str();
    return query;
  }

  auto hasQueryPermission(const std::string &creator,
                          const std::string &target_account,
                          Role indiv_permission_id,
                          Role all_permission_id,
                          Role domain_permission_id) {
    const auto bits = shared_model::interface::RolePermissionSet::size();
    const auto perm_str =
        shared_model::interface::RolePermissionSet({indiv_permission_id})
            .toBitstring();
    const auto all_perm_str =
        shared_model::interface::RolePermissionSet({all_permission_id})
            .toBitstring();
    const auto domain_perm_str =
        shared_model::interface::RolePermissionSet({domain_permission_id})
            .toBitstring();

    boost::format cmd(R"(
    WITH
        has_indiv_perm AS (
          SELECT (COALESCE(bit_or(rp.permission), '0'::bit(%1%))
          & '%3%') = '%3%' FROM role_has_permissions AS rp
              JOIN account_has_roles AS ar on ar.role_id = rp.role_id
              WHERE ar.account_id = '%2%'
        ),
        has_all_perm AS (
          SELECT (COALESCE(bit_or(rp.permission), '0'::bit(%1%))
          & '%4%') = '%4%' FROM role_has_permissions AS rp
              JOIN account_has_roles AS ar on ar.role_id = rp.role_id
              WHERE ar.account_id = '%2%'
        ),
        has_domain_perm AS (
          SELECT (COALESCE(bit_or(rp.permission), '0'::bit(%1%))
          & '%5%') = '%5%' FROM role_has_permissions AS rp
              JOIN account_has_roles AS ar on ar.role_id = rp.role_id
              WHERE ar.account_id = '%2%'
        )
    SELECT ('%2%' = '%6%' AND (SELECT * FROM has_indiv_perm))
        OR (SELECT * FROM has_all_perm)
        OR ('%7%' = '%8%' AND (SELECT * FROM has_domain_perm)) AS perm
    )");

    return (cmd % bits % creator % perm_str % all_perm_str % domain_perm_str
            % target_account % getDomainFromName(creator)
            % getDomainFromName(target_account))
        .str();
  }

  void callback(
      std::vector<std::shared_ptr<shared_model::interface::Transaction>>
          &blocks,
      uint64_t block_id,
      ametsuchi::KeyValueStorage &block_store,
      std::vector<uint64_t> &result) {
    auto block = block_store.get(block_id) | [](const auto &bytes) {
      return shared_model::converters::protobuf::jsonToModel<
          shared_model::proto::Block>(bytesToString(bytes));
    };
    if (not block) {
      return;
    }

    std::transform(
        result.begin(),
        result.end(),
        std::back_inserter(blocks),
        [&](const auto &x) {
          return std::shared_ptr<shared_model::interface::Transaction>(
              clone(block->transactions()[x]));
        });
  }
}  // namespace

namespace iroha {
  namespace ametsuchi {
    PostgresQueryExecutor::PostgresQueryExecutor(
        std::unique_ptr<soci::session> sql,
        std::shared_ptr<shared_model::interface::CommonObjectsFactory> factory,
        KeyValueStorage &block_store,
        std::shared_ptr<PendingTransactionStorage> pending_txs_storage)
        : sql_(std::move(sql)),
          block_store_(block_store),
          factory_(factory),
          pending_txs_storage_(pending_txs_storage),
          visitor_(*sql_, factory_, block_store_, pending_txs_storage_) {}

    QueryExecutorResult PostgresQueryExecutor::validateAndExecute(
        const shared_model::interface::Query &query) {
      visitor_.setCreatorId(query.creatorAccountId());
      auto result = boost::apply_visitor(visitor_, query.get());
      return clone(result.queryHash(query.hash()).build());
    }

    bool PostgresQueryExecutor::validate(
        const shared_model::interface::BlocksQuery &query) {
      boost::format cmd(R"(%s)");
      soci::statement st =
          (sql_->prepare
           << (cmd % checkAccountRolePermission(Role::kGetBlocks)).str());
      int has_perm;
      st.exchange(soci::use(query.creatorAccountId(), "role_account_id"));
      st.exchange(soci::into(has_perm));
      st.define_and_bind();
      st.execute(true);

      return has_perm > 0;
    }

    PostgresQueryExecutorVisitor::PostgresQueryExecutorVisitor(
        soci::session &sql,
        std::shared_ptr<shared_model::interface::CommonObjectsFactory> factory,
        KeyValueStorage &block_store,
        std::shared_ptr<PendingTransactionStorage> pending_txs_storage)
        : sql_(sql),
          block_store_(block_store),
          factory_(factory),
          pending_txs_storage_(pending_txs_storage) {}

    void PostgresQueryExecutorVisitor::setCreatorId(
        const shared_model::interface::types::AccountIdType &creator_id) {
      creator_id_ = creator_id;
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetAccount &q) {
      using T = boost::tuple<
          boost::optional<shared_model::interface::types::AccountIdType>,
          boost::optional<shared_model::interface::types::DomainIdType>,
          boost::optional<shared_model::interface::types::QuorumType>,
          boost::optional<shared_model::interface::types::DetailType>,
          boost::optional<shared_model::interface::types::RoleIdType>,
          int>;
      T tuple;
      auto cmd = (boost::format(R"(WITH has_perms AS (%s),
      t AS (
          SELECT a.account_id, a.domain_id, a.quorum, a.data, ARRAY_AGG(ar.role_id) AS roles
          FROM account AS a, account_has_roles AS ar
          WHERE a.account_id = :target_account_id
          AND ar.account_id = a.account_id
          GROUP BY a.account_id
      )
      SELECT account_id, domain_id, quorum, data, roles, perm
      FROM t RIGHT OUTER JOIN has_perms AS p ON TRUE
      )")
                  % hasQueryPermission(creator_id_,
                                       q.accountId(),
                                       Role::kGetMyAccount,
                                       Role::kGetAllAccounts,
                                       Role::kGetDomainAccounts))
                     .str();
      soci::statement st = (sql_.prepare << cmd);
      st.exchange(soci::use(q.accountId(), "target_account_id"));
      st.exchange(soci::into(tuple));

      st.define_and_bind();
      st.execute(true);

      if (not tuple.get<5>()) {
        return statefulFailed();
      }

      if (not tuple.get<0>().is_initialized()) {
        return buildError<shared_model::interface::NoAccountErrorResponse>();
      }

      auto roles_str = tuple.get<4>().get();
      roles_str.erase(0, 1);
      roles_str.erase(roles_str.size() - 1, 1);

      std::vector<shared_model::interface::types::RoleIdType> roles;

      boost::split(roles, roles_str, [](char c) { return c == ','; });

      auto account = fromResult(factory_->createAccount(q.accountId(),
                                                        tuple.get<1>().get(),
                                                        tuple.get<2>().get(),
                                                        tuple.get<3>().get()));

      if (not account) {
        return statefulFailed();
      }

      auto response = QueryResponseBuilder().accountResponse(
          *std::static_pointer_cast<shared_model::proto::Account>(*account),
          roles);
      return response;
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetSignatories &q) {
      std::vector<shared_model::interface::types::PubkeyType> pubkeys;
      soci::indicator ind;
      boost::tuple<boost::optional<std::string>, int> row;
      auto cmd = (boost::format(R"(WITH has_perms AS (%s),
      t AS (
          SELECT public_key FROM account_has_signatory
          WHERE account_id = :account_id
      )
      SELECT public_key, perm FROM t
      RIGHT OUTER JOIN has_perms ON TRUE
      )")
                  % hasQueryPermission(creator_id_,
                                       q.accountId(),
                                       Role::kGetMySignatories,
                                       Role::kGetAllSignatories,
                                       Role::kGetDomainSignatories))
                     .str();
      soci::statement st = (sql_.prepare << cmd);
      st.exchange(soci::use(q.accountId()));
      st.exchange(soci::into(row, ind));

      st.define_and_bind();
      st.execute();

      int has_perm = -1;

      processSoci(st,
                  ind,
                  row,
                  [&pubkeys, &has_perm](
                      boost::tuple<boost::optional<std::string>, int> &row) {
                    has_perm = row.get<1>();
                    if (row.get<0>()) {
                      pubkeys.push_back(shared_model::crypto::PublicKey(
                          shared_model::crypto::Blob::fromHexString(
                              row.get<0>().get())));
                    }
                  });

      if (not has_perm) {
        return statefulFailed();
      }

      if (pubkeys.empty()) {
        return buildError<
            shared_model::interface::NoSignatoriesErrorResponse>();
      }
      return QueryResponseBuilder().signatoriesResponse(pubkeys);
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetAccountTransactions &q) {
      soci::indicator ind;
      using T = boost::
          tuple<boost::optional<uint64_t>, boost::optional<uint64_t>, int>;
      T row;
      auto cmd = (boost::format(R"(WITH has_perms AS (%s),
      t AS (
          SELECT DISTINCT has.height, index
          FROM height_by_account_set AS has
          JOIN index_by_creator_height AS ich ON has.height = ich.height
          AND has.account_id = ich.creator_id
          WHERE account_id = :account_id
          ORDER BY has.height, index ASC
      )
      SELECT height, index, perm FROM t
      RIGHT OUTER JOIN has_perms ON TRUE
      )")
                  % hasQueryPermission(creator_id_,
                                       q.accountId(),
                                       Role::kGetMyAccTxs,
                                       Role::kGetAllAccTxs,
                                       Role::kGetDomainAccTxs))
                     .str();
      soci::statement st = (sql_.prepare << cmd);
      st.exchange(soci::use(q.accountId()));
      st.exchange(soci::into(row, ind));

      st.define_and_bind();
      st.execute();

      int has_perm = -1;

      std::map<uint64_t, std::vector<uint64_t>> index;

      processSoci(st, ind, row, [&index, &has_perm](T &row) {
        has_perm = row.get<2>();
        if (row.get<0>()) {
          if (index.find(row.get<0>().get()) == index.end()) {
            index[row.get<0>().get()] = std::vector<uint64_t>();
          }
          index[row.get<0>().get()].push_back(row.get<1>().get());
        }
      });

      if (not has_perm) {
        return statefulFailed();
      }

      std::vector<shared_model::proto::Transaction> proto;
      std::vector<std::shared_ptr<shared_model::interface::Transaction>> txs;
      for (auto &block : index) {
        callback(txs, block.first, block_store_, block.second);
      }

      std::transform(
          txs.begin(),
          txs.end(),
          std::back_inserter(proto),
          [](const auto &tx) {
            return *std::static_pointer_cast<shared_model::proto::Transaction>(
                tx);
          });

      auto response = QueryResponseBuilder().transactionsResponse(proto);
      return response;
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetTransactions &q) {
      std::string hash_str;

      for (size_t i = 0; i < q.transactionHashes().size(); i++) {
        if (i > 0) {
          hash_str += ",";
        }
        hash_str += "'" + q.transactionHashes()[i].hex() + "'";
      }

      soci::indicator ind;
      using T = boost::tuple<
          boost::optional<shared_model::interface::types::HeightType>,
          boost::optional<std::string>,
          int,
          int>;
      T row;
      auto cmd = (boost::format(R"(WITH has_my_perm AS (%s),
      has_all_perm AS (%s),
      t AS (
          SELECT height, hash FROM height_by_hash WHERE hash IN (%s)
      )
      SELECT height, hash, has_my_perm.perm, has_all_perm.perm FROM t
      RIGHT OUTER JOIN has_my_perm ON TRUE
      RIGHT OUTER JOIN has_all_perm ON TRUE
      )") % checkAccountRolePermission(Role::kGetMyTxs, "account_id")
                  % checkAccountRolePermission(Role::kGetAllTxs, "account_id")
                  % hash_str)
                     .str();
      auto s = sql_.prepare << cmd;
      int has_my_perm = -1;
      int has_all_perm = -1;
      std::map<uint64_t, std::vector<std::string>> index;
      soci::statement st = s;
      st.exchange(soci::use(creator_id_, "account_id"));
      st.exchange(soci::into(row, ind));

      st.define_and_bind();
      try {
        st.execute();
      } catch (std::exception &e) {
        e.what();
      }

      processSoci(st, ind, row, [&](T &row) {
        has_my_perm = row.get<2>();
        has_all_perm = row.get<3>();
        if (row.get<0>()) {
          if (index.find(row.get<0>().get()) == index.end()) {
            index[row.get<0>().get()] = std::vector<std::string>();
          }
          index[row.get<0>().get()].push_back(row.get<1>().get());
        }
      });

      if (not has_my_perm and not has_all_perm) {
        return statefulFailed();
      }

      std::vector<shared_model::proto::Transaction> proto;
      std::vector<std::shared_ptr<shared_model::interface::Transaction>> txs;
      for (auto &block : index) {
        auto b = block_store_.get(block.first) | [](const auto &bytes) {
          return shared_model::converters::protobuf::jsonToModel<
              shared_model::proto::Block>(bytesToString(bytes));
        };
        if (not b) {
          break;
        }

        boost::for_each(block.second, [&](const auto &x) {
          boost::for_each(b->transactions(), [&](const auto &tx) {
            if (tx.hash().hex() == x
                and (has_all_perm
                     or (has_my_perm
                         and tx.creatorAccountId() == creator_id_))) {
              txs.push_back(
                  std::shared_ptr<shared_model::interface::Transaction>(
                      clone(tx)));
            }
          });
        });
      }

      std::transform(
          txs.begin(),
          txs.end(),
          std::back_inserter(proto),
          [](const auto &tx) {
            return *std::static_pointer_cast<shared_model::proto::Transaction>(
                tx);
          });

      auto response = QueryResponseBuilder().transactionsResponse(proto);
      return response;
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetAccountAssetTransactions &q) {
      soci::indicator ind;
      using T = boost::
          tuple<boost::optional<uint64_t>, boost::optional<uint64_t>, int>;
      T row;
      auto cmd = (boost::format(R"(WITH has_perms AS (%s),
      t AS (
          SELECT DISTINCT has.height, index
          FROM height_by_account_set AS has
          JOIN index_by_id_height_asset AS ich ON has.height = ich.height
          AND has.account_id = ich.id
          WHERE account_id = :account_id
          AND asset_id = :asset_id
          ORDER BY has.height, index ASC
      )
      SELECT height, index, perm FROM t
      RIGHT OUTER JOIN has_perms ON TRUE
      )")
                  % hasQueryPermission(creator_id_,
                                       q.accountId(),
                                       Role::kGetMyAccAstTxs,
                                       Role::kGetAllAccAstTxs,
                                       Role::kGetDomainAccAstTxs))
                     .str();
      soci::statement st = (sql_.prepare << cmd);
      st.exchange(soci::use(q.accountId(), "account_id"));
      st.exchange(soci::use(q.assetId(), "asset_id"));
      st.exchange(soci::into(row, ind));

      st.define_and_bind();
      st.execute();

      int has_perm = -1;

      std::map<uint64_t, std::vector<uint64_t>> index;

      processSoci(st, ind, row, [&index, &has_perm](T &row) {
        has_perm = row.get<2>();
        if (row.get<0>()) {
          if (index.find(row.get<0>().get()) == index.end()) {
            index[row.get<0>().get()] = std::vector<uint64_t>();
          }
          index[row.get<0>().get()].push_back(row.get<1>().get());
        }
      });

      if (not has_perm) {
        return statefulFailed();
      }

      std::vector<shared_model::proto::Transaction> proto;
      std::vector<std::shared_ptr<shared_model::interface::Transaction>> txs;
      for (auto &block : index) {
        callback(txs, block.first, block_store_, block.second);
      }

      std::transform(
          txs.begin(),
          txs.end(),
          std::back_inserter(proto),
          [](const auto &tx) {
            return *std::static_pointer_cast<shared_model::proto::Transaction>(
                tx);
          });

      auto response = QueryResponseBuilder().transactionsResponse(proto);
      return response;
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetAccountAssets &q) {
      soci::indicator ind;
      using T = boost::tuple<
          boost::optional<shared_model::interface::types::AccountIdType>,
          boost::optional<shared_model::interface::types::AssetIdType>,
          boost::optional<std::string>,
          int>;
      T row;
      auto cmd = (boost::format(R"(WITH has_perms AS (%s),
      t AS (
          SELECT * FROM account_has_asset
          WHERE account_id = :account_id
      )
      SELECT account_id, asset_id, amount, perm FROM t
      RIGHT OUTER JOIN has_perms ON TRUE
      )")
                  % hasQueryPermission(creator_id_,
                                       q.accountId(),
                                       Role::kGetMyAccAst,
                                       Role::kGetAllAccAst,
                                       Role::kGetDomainAccAst))
                     .str();
      soci::statement st = (sql_.prepare << cmd);
      st.exchange(soci::use(q.accountId()));
      st.exchange(soci::into(row, ind));

      st.define_and_bind();
      st.execute();

      int has_perm = -1;

      std::vector<shared_model::proto::AccountAsset> account_assets;
      processSoci(st, ind, row, [&account_assets, &has_perm, this](T &row) {
        has_perm = row.get<3>();
        if (row.get<0>()) {
          fromResult(factory_->createAccountAsset(
              row.get<0>().get(),
              row.get<1>().get(),
              shared_model::interface::Amount(row.get<2>().get())))
              | [&account_assets](const auto &asset) {
                  auto proto = *std::static_pointer_cast<
                      shared_model::proto::AccountAsset>(asset);
                  account_assets.push_back(proto);
                };
        }
      });

      if (not has_perm) {
        return statefulFailed();
      }
      return QueryResponseBuilder().accountAssetResponse(account_assets);
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetAccountDetail &q) {
      boost::tuple<boost::optional<shared_model::interface::types::DetailType>,
                   int>
          row;
      std::string query_detail;
      if (q.key() and q.writer()) {
        auto filled_json = (boost::format("{\"%s\", \"%s\"}") % q.writer().get()
                            % q.key().get());
        query_detail = (boost::format(R"(SELECT json_build_object('%s'::text,
            json_build_object('%s'::text, (SELECT data #>> '%s'
            FROM account WHERE account_id = :account_id))) AS json)")
                        % q.writer().get() % q.key().get() % filled_json)
                           .str();
      } else if (q.key() and not q.writer()) {
        query_detail =
            (boost::format(
                 R"(SELECT json_object_agg(key, value) AS json FROM (SELECT
            json_build_object(kv.key, json_build_object('%1%'::text,
            kv.value -> '%1%')) FROM jsonb_each((SELECT data FROM account
            WHERE account_id = :account_id)) kv WHERE kv.value ? '%1%') AS
            jsons, json_each(json_build_object))")
             % q.key().get())
                .str();
      } else if (not q.key() and q.writer()) {
        query_detail = (boost::format(R"(SELECT json_build_object('%1%'::text,
          (SELECT data -> '%1%' FROM account WHERE account_id =
           :account_id)) AS json)")
                        % q.writer().get())
                           .str();
      } else {
        query_detail = (boost::format(R"(SELECT data#>>'{}' AS json FROM account
            WHERE account_id = :account_id)"))
                           .str();
      }
      auto cmd = (boost::format(R"(WITH has_perms AS (%s),
      detail AS (%s)
      SELECT json, perm FROM detail
      RIGHT OUTER JOIN has_perms ON TRUE
      )")
                  % hasQueryPermission(creator_id_,
                                       q.accountId(),
                                       Role::kGetMyAccDetail,
                                       Role::kGetAllAccDetail,
                                       Role::kGetDomainAccDetail)
                  % query_detail)
                     .str();
      soci::statement st = (sql_.prepare << cmd);
      st.exchange(soci::use(q.accountId(), "account_id"));
      st.exchange(soci::into(row));

      st.define_and_bind();
      st.execute(true);

      if (not row.get<1>()) {
        return statefulFailed();
      }

      if (not row.get<0>().is_initialized()) {
        return buildError<
            shared_model::interface::NoAccountDetailErrorResponse>();
      }

      auto response =
          QueryResponseBuilder().accountDetailResponse(row.get<0>().get());
      return response;
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetRoles &q) {
      soci::indicator ind;
      using T = boost::tuple<
          boost::optional<shared_model::interface::types::RoleIdType>,
          int>;
      T row;
      auto cmd = (boost::format(
                      R"(WITH has_perms AS (%s)
      SELECT role_id, perm FROM role
      RIGHT OUTER JOIN has_perms ON TRUE
      )") % checkAccountRolePermission(Role::kGetRoles))
                     .str();
      soci::statement st = (sql_.prepare << cmd);
      st.exchange(soci::use(creator_id_, "role_account_id"));
      st.exchange(soci::into(row, ind));

      st.define_and_bind();
      st.execute();

      std::vector<shared_model::interface::types::RoleIdType> roles;
      int has_perm;

      processSoci(st, ind, row, [&roles, &has_perm](T &row) {
        has_perm = row.get<1>();
        if (row.get<0>().is_initialized()) {
          roles.push_back(row.get<0>().get());
        }
      });

      if (not has_perm) {
        return statefulFailed();
      }

      if (roles.empty()) {
        return buildError<shared_model::interface::NoRolesErrorResponse>();
      }
      auto response = QueryResponseBuilder().rolesResponse(roles);
      return response;
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetRolePermissions &q) {
      using T = boost::tuple<boost::optional<std::string>, int>;
      T row;
      auto cmd = (boost::format(
                      R"(WITH has_perms AS (%s),
      perms AS (SELECT permission FROM role_has_permissions
                WHERE role_id = :role_name)
      SELECT permission, perm FROM perms
      RIGHT OUTER JOIN has_perms ON TRUE
      )") % checkAccountRolePermission(Role::kGetRoles))
                     .str();
      soci::statement st = (sql_.prepare << cmd);
      st.exchange(soci::use(creator_id_, "role_account_id"));
      st.exchange(soci::use(q.roleId(), "role_name"));
      st.exchange(soci::into(row));
      shared_model::interface::RolePermissionSet set;

      st.define_and_bind();
      st.execute(true);

      if (not row.get<1>()) {
        return statefulFailed();
      }

      if (not row.get<0>()) {
        return buildError<shared_model::interface::NoRolesErrorResponse>();
      }
      auto response = QueryResponseBuilder().rolePermissionsResponse(
          shared_model::interface::RolePermissionSet(row.get<0>().get()));
      return response;
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetAssetInfo &q) {
      using T = boost::
          tuple<boost::optional<std::string>, boost::optional<uint32_t>, int>;
      T row;
      auto cmd = (boost::format(
                      R"(WITH has_perms AS (%s),
      perms AS (SELECT domain_id, precision FROM asset
                WHERE asset_id = :asset_id)
      SELECT domain_id, precision, perm FROM perms
      RIGHT OUTER JOIN has_perms ON TRUE
      )") % checkAccountRolePermission(Role::kReadAssets))
                     .str();
      soci::statement st = (sql_.prepare << cmd);
      st.exchange(soci::use(creator_id_, "role_account_id"));
      st.exchange(soci::use(q.assetId(), "asset_id"));
      st.exchange(soci::into(row));
      shared_model::interface::RolePermissionSet set;

      st.define_and_bind();
      st.execute(true);

      if (not row.get<2>()) {
        return statefulFailed();
      }

      if (not row.get<0>().is_initialized()) {
        return buildError<shared_model::interface::NoAssetErrorResponse>();
      }
      auto response = QueryResponseBuilder().assetResponse(
          q.assetId(), row.get<0>().get(), row.get<1>().get());
      return response;
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetPendingTransactions &q) {
      std::vector<shared_model::proto::Transaction> txs;
      // TODO 2018-07-04, igor-egorov, IR-1486, the core logic is to be
      // implemented
      auto response = QueryResponseBuilder().transactionsResponse(txs);
      return response;
    }
  }  // namespace ametsuchi
}  // namespace iroha
