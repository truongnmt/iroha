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
#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/algorithm/transform.hpp>
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

  std::string getDomainFromName(const std::string &account_id) {
    std::vector<std::string> res;
    boost::split(res, account_id, boost::is_any_of("@"));
    return res.at(1);
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

  /**
   * Get transactions from block storage by block id and a list of tx indices
   */
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

  /// Query result is a tuple of optionals, since there could be no entry
  template <typename... Value>
  using QueryType = boost::tuple<boost::optional<Value>...>;

  /// tuple length shortcut
  template <typename T>
  constexpr std::size_t length_v = boost::tuples::length<T>::value;

  /// tuple element type shortcut
  template <std::size_t N, typename T>
  using element_t = typename boost::tuples::element<N, T>::type;

  /// index sequence helper for concat
  template <class Tuple1, class Tuple2, std::size_t... Is, std::size_t... Js>
  auto concat_impl(std::index_sequence<Is...>, std::index_sequence<Js...>)
      -> boost::tuple<element_t<Is, std::decay_t<Tuple1>>...,
                      element_t<Js, std::decay_t<Tuple2>>...>;

  /// tuple with types from two given tuples
  template <class Tuple1, class Tuple2>
  using concat = decltype(concat_impl<Tuple1, Tuple2>(
      std::make_index_sequence<length_v<std::decay_t<Tuple1>>>{},
      std::make_index_sequence<length_v<std::decay_t<Tuple2>>>{}));

  /// index sequence helper for index_apply
  template <typename F, std::size_t... Is>
  constexpr decltype(auto) index_apply_impl(F &&f, std::index_sequence<Is...>) {
    return std::forward<F>(f)(std::integral_constant<std::size_t, Is>{}...);
  }

  /// apply F to an integer sequence [0, N)
  template <size_t N, typename F>
  constexpr decltype(auto) index_apply(F &&f) {
    return index_apply_impl(std::forward<F>(f), std::make_index_sequence<N>{});
  }

  /// apply F to Tuple
  template <typename Tuple, typename F>
  constexpr decltype(auto) apply(Tuple &&t, F &&f) {
    return index_apply<length_v<std::decay_t<Tuple>>>(
        [&](auto... Is) -> decltype(auto) {
          return std::forward<F>(f)(
              std::forward<Tuple>(t).template get<Is>()...);
        });
  }

  /// view first length_v<R> elements of T without copying
  template <typename R, typename T>
  constexpr auto viewTuple(T &&t) {
    return index_apply<length_v<std::decay_t<R>>>([&](auto... Is) {
      return boost::make_tuple(std::forward<T>(t).template get<Is>()...);
    });
  }

  /// view last length_v<R> elements of T without copying
  template <typename R, typename T>
  constexpr auto viewRest(T &&t) {
    return index_apply<length_v<std::decay_t<R>>>([&](auto... Is) {
      return boost::make_tuple(
          std::forward<T>(t)
              .template get<Is
                            + length_v<std::decay_t<
                                  T>> - length_v<std::decay_t<R>>>()...);
    });
  }

  /// apply M to optional T
  template <typename T, typename M>
  constexpr decltype(auto) match(T &&t, M &&m) {
    return std::forward<T>(t) ? std::forward<M>(m)(*std::forward<T>(t))
                              : std::forward<M>(m)();
  }

  /// construct visitor from Fs and apply it to optional T
  template <typename T, typename... Fs>
  constexpr decltype(auto) match_in_place(T &&t, Fs &&... fs) {
    return match(std::forward<T>(t), make_visitor(std::forward<Fs>(fs)...));
  }

  /// map tuple<optional<Ts>...> to optional<tuple<Ts...>>
  template <typename T>
  constexpr auto rebind(T &&t) {
    auto transform = [](auto &&... vals) {
      return boost::make_tuple(*std::forward<decltype(vals)>(vals)...);
    };

    using ReturnType =
        decltype(boost::make_optional(apply(std::forward<T>(t), transform)));

    return apply(std::forward<T>(t),
                 [&](auto &&... vals) {
                   bool temp[] = {static_cast<bool>(
                       std::forward<decltype(vals)>(vals))...};
                   return std::all_of(std::begin(temp),
                                      std::end(temp),
                                      [](auto b) { return b; });
                 })
        ? boost::make_optional(apply(std::forward<T>(t), transform))
        : ReturnType{};
  }

  /// return statefulFailed if no permissions in given tuple are set, nullopt
  /// otherwise
  template <typename T>
  auto validatePermissions(const T &t) {
    return apply(t, [](auto... perms) {
      bool temp[] = {not perms...};
      return boost::make_optional<ametsuchi::QueryResponseBuilderDone>(
          std::all_of(
              std::begin(temp), std::end(temp), [](auto b) { return b; }),
          statefulFailed());
    });
  }

}  // namespace

namespace iroha {
  namespace ametsuchi {

    using QueryResponseBuilder =
        shared_model::proto::TemplateQueryResponseBuilder<0>;

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
      using T = boost::tuple<int>;
      boost::format cmd(R"(%s)");
      soci::rowset<T> st =
          (sql_->prepare
               << (cmd % checkAccountRolePermission(Role::kGetBlocks)).str(),
           soci::use(query.creatorAccountId(), "role_account_id"));

      return st.begin()->get<0>();
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
      using Q = QueryType<shared_model::interface::types::AccountIdType,
                          shared_model::interface::types::DomainIdType,
                          shared_model::interface::types::QuorumType,
                          shared_model::interface::types::DetailType,
                          shared_model::interface::types::RoleIdType>;
      using P = boost::tuple<int>;
      using T = concat<Q, P>;

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

      soci::rowset<T> st =
          (sql_.prepare << cmd, soci::use(q.accountId(), "target_account_id"));
      auto &tuple = *st.begin();

      auto query_apply = [this](auto &account_id,
                                auto &domain_id,
                                auto &quorum,
                                auto &data,
                                auto &roles_str) {
        return match_in_place(
            fromResult(
                factory_->createAccount(account_id, domain_id, quorum, data)),
            [&roles_str](auto account) {
              roles_str.erase(0, 1);
              roles_str.erase(roles_str.size() - 1, 1);

              std::vector<shared_model::interface::types::RoleIdType> roles;

              boost::split(roles, roles_str, [](char c) { return c == ','; });

              return QueryResponseBuilder().accountResponse(
                  *std::static_pointer_cast<shared_model::proto::Account>(
                      account),
                  roles);
            },
            [] { return statefulFailed(); });
      };

      return validatePermissions(viewRest<P>(tuple))
          .value_or_eval([this, &tuple, &query_apply] {
            return match_in_place(
                rebind(viewTuple<Q>(tuple)),
                [this, &query_apply](auto &&t) {
                  return apply(t, query_apply);
                },
                [] {
                  return buildError<
                      shared_model::interface::NoAccountErrorResponse>();
                });
          });
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetSignatories &q) {
      using Q = QueryType<std::string>;
      using P = boost::tuple<int>;
      using T = concat<Q, P>;

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

      soci::rowset<T> st = (sql_.prepare << cmd, soci::use(q.accountId()));
      // get iterators since they are single pass
      auto begin = st.begin(), end = st.end();

      return validatePermissions(viewRest<P>(*begin))
          .value_or_eval([&begin, &end] {
            std::vector<shared_model::interface::types::PubkeyType> pubkeys;
            std::for_each(begin, end, [&pubkeys](auto &t) {
              rebind(viewTuple<Q>(t)) | [&pubkeys](auto &&t) {
                apply(t, [&pubkeys](auto &public_key) {
                  pubkeys.emplace_back(
                      shared_model::crypto::Blob::fromHexString(public_key));
                });
              };
            });
            return boost::make_optional<QueryResponseBuilderDone>(
                       pubkeys.empty(),
                       buildError<shared_model::interface::
                                      NoSignatoriesErrorResponse>())
                .value_or_eval([&pubkeys] {
                  return QueryResponseBuilder().signatoriesResponse(pubkeys);
                });
          });
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetAccountTransactions &q) {
      using Q = QueryType<uint64_t, uint64_t>;
      using P = boost::tuple<int>;
      using T = concat<Q, P>;

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
      soci::rowset<T> st = (sql_.prepare << cmd, soci::use(q.accountId()));
      auto begin = st.begin(), end = st.end();

      auto deserialize = [this, &begin, &end] {
        std::map<uint64_t, std::vector<uint64_t>> index;
        std::for_each(begin, end, [&index](auto &t) {
          rebind(viewTuple<Q>(t)) | [&index](auto &&t) {
            apply(t, [&index](auto &height, auto &idx) {
              index[height].push_back(idx);
            });
          };
        });

        std::vector<std::shared_ptr<shared_model::interface::Transaction>> txs;
        for (auto &block : index) {
          callback(txs, block.first, block_store_, block.second);
        }

        std::vector<shared_model::proto::Transaction> proto;
        std::transform(txs.begin(),
                       txs.end(),
                       std::back_inserter(proto),
                       [](const auto &tx) {
                         return *std::static_pointer_cast<
                             shared_model::proto::Transaction>(tx);
                       });

        return QueryResponseBuilder().transactionsResponse(proto);
      };

      return validatePermissions(viewRest<P>(*begin))
          .value_or_eval(deserialize);
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

      using Q =
          QueryType<shared_model::interface::types::HeightType, std::string>;
      using P = boost::tuple<int, int>;
      using T = concat<Q, P>;

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
      soci::rowset<T> st =
          (sql_.prepare << cmd, soci::use(creator_id_, "account_id"));
      auto begin = st.begin(), end = st.end();

      auto deserialize = [this, &begin, &end](auto &my_perm, auto &all_perm) {
        std::cout << my_perm << " " << all_perm << std::endl;
        std::map<uint64_t, std::vector<std::string>> index;
        std::for_each(begin, end, [&index](auto &t) {
          rebind(viewTuple<Q>(t)) | [&index](auto &&t) {
            apply(t, [&index](auto &height, auto &hash) {
              index[height].push_back(hash);
            });
          };
        });

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
                  and (all_perm
                       or (my_perm and tx.creatorAccountId() == creator_id_))) {
                txs.push_back(
                    std::shared_ptr<shared_model::interface::Transaction>(
                        clone(tx)));
              }
            });
          });
        }

        std::vector<shared_model::proto::Transaction> proto;
        std::transform(txs.begin(),
                       txs.end(),
                       std::back_inserter(proto),
                       [](const auto &tx) {
                         return *std::static_pointer_cast<
                             shared_model::proto::Transaction>(tx);
                       });

        return QueryResponseBuilder().transactionsResponse(proto);
      };

      return validatePermissions(viewRest<P>(*begin))
          .value_or_eval([this, &begin, &end, &deserialize] {
            return apply(viewRest<P>(*begin), deserialize);
          });
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetAccountAssetTransactions &q) {
      using Q = QueryType<uint64_t, uint64_t>;
      using P = boost::tuple<int>;
      using T = concat<Q, P>;

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

      soci::rowset<T> st = (sql_.prepare << cmd,
                            soci::use(q.accountId(), "account_id"),
                            soci::use(q.assetId(), "asset_id"));
      auto begin = st.begin(), end = st.end();

      auto deserialize = [this, &begin, &end] {
        std::map<uint64_t, std::vector<uint64_t>> index;
        std::for_each(begin, end, [&index](auto &t) {
          rebind(viewTuple<Q>(t)) | [&index](auto &&t) {
            apply(t, [&index](auto &height, auto &idx) {
              index[height].push_back(idx);
            });
          };
        });

        std::vector<std::shared_ptr<shared_model::interface::Transaction>> txs;
        for (auto &block : index) {
          callback(txs, block.first, block_store_, block.second);
        }

        std::vector<shared_model::proto::Transaction> proto;
        std::transform(txs.begin(),
                       txs.end(),
                       std::back_inserter(proto),
                       [](const auto &tx) {
                         return *std::static_pointer_cast<
                             shared_model::proto::Transaction>(tx);
                       });

        return QueryResponseBuilder().transactionsResponse(proto);
      };

      return validatePermissions(viewRest<P>(*begin))
          .value_or_eval(deserialize);
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetAccountAssets &q) {
      using Q = QueryType<shared_model::interface::types::AccountIdType,
                          shared_model::interface::types::AssetIdType,
                          std::string>;
      using P = boost::tuple<int>;
      using T = concat<Q, P>;

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
      soci::rowset<T> st = (sql_.prepare << cmd, soci::use(q.accountId()));
      auto begin = st.begin(), end = st.end();

      return validatePermissions(viewRest<P>(*begin))
          .value_or_eval([this, &begin, &end] {
            std::vector<shared_model::proto::AccountAsset> account_assets;
            std::for_each(begin, end, [this, &account_assets](auto &t) {
              rebind(viewTuple<Q>(t)) | [this, &account_assets](auto &&t) {
                apply(t,
                      [this, &account_assets](
                          auto &account_id, auto &asset_id, auto &amount) {
                        fromResult(factory_->createAccountAsset(
                            account_id,
                            asset_id,
                            shared_model::interface::Amount(amount)))
                            | [&account_assets](const auto &asset) {
                                auto proto = *std::static_pointer_cast<
                                    shared_model::proto::AccountAsset>(asset);
                                account_assets.push_back(proto);
                              };
                      });
              };
            });
            return boost::make_optional<QueryResponseBuilderDone>(
                       account_assets.empty(),
                       buildError<shared_model::interface::
                                      NoAccountAssetsErrorResponse>())
                .value_or_eval([&account_assets] {
                  return QueryResponseBuilder().accountAssetResponse(
                      account_assets);
                });
          });
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetAccountDetail &q) {
      using Q = QueryType<shared_model::interface::types::DetailType>;
      using P = boost::tuple<int>;
      using T = concat<Q, P>;

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
      soci::rowset<T> st =
          (sql_.prepare << cmd, soci::use(q.accountId(), "account_id"));
      auto &tuple = *st.begin();

      return validatePermissions(viewRest<P>(tuple))
          .value_or_eval([this, &tuple] {
            return match_in_place(
                rebind(viewTuple<Q>(tuple)),
                [this](auto &&t) {
                  return apply(t, [this](auto &json) {
                    return QueryResponseBuilder().accountDetailResponse(json);
                  });
                },
                [] {
                  return buildError<
                      shared_model::interface::NoAccountDetailErrorResponse>();
                });
          });
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetRoles &q) {
      using Q = QueryType<shared_model::interface::types::RoleIdType>;
      using P = boost::tuple<int>;
      using T = concat<Q, P>;

      auto cmd = (boost::format(
                      R"(WITH has_perms AS (%s)
      SELECT role_id, perm FROM role
      RIGHT OUTER JOIN has_perms ON TRUE
      )") % checkAccountRolePermission(Role::kGetRoles))
                     .str();
      soci::rowset<T> st =
          (sql_.prepare << cmd, soci::use(creator_id_, "role_account_id"));
      auto begin = st.begin(), end = st.end();

      return validatePermissions(viewRest<P>(*begin))
          .value_or_eval([&begin, &end] {
            std::vector<shared_model::interface::types::RoleIdType> roles;
            std::for_each(begin, end, [&roles](auto &t) {
              rebind(viewTuple<Q>(t)) | [&roles](auto &&t) {
                apply(t, [&roles](auto &role_id) { roles.push_back(role_id); });
              };
            });

            // roles vector is never empty, since an account is required to
            // perform a query, and therefore a domain

            return QueryResponseBuilder().rolesResponse(roles);
          });
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetRolePermissions &q) {
      using Q = QueryType<std::string>;
      using P = boost::tuple<int>;
      using T = concat<Q, P>;

      auto cmd = (boost::format(
                      R"(WITH has_perms AS (%s),
      perms AS (SELECT permission FROM role_has_permissions
                WHERE role_id = :role_name)
      SELECT permission, perm FROM perms
      RIGHT OUTER JOIN has_perms ON TRUE
      )") % checkAccountRolePermission(Role::kGetRoles))
                     .str();

      soci::rowset<T> st = (sql_.prepare << cmd,
                            soci::use(creator_id_, "role_account_id"),
                            soci::use(q.roleId(), "role_name"));
      auto &tuple = *st.begin();

      return validatePermissions(viewRest<P>(tuple))
          .value_or_eval([this, &tuple] {
            return match_in_place(
                rebind(viewTuple<Q>(tuple)),
                [this](auto &&t) {
                  return apply(t, [this](auto &permission) {
                    return QueryResponseBuilder().rolePermissionsResponse(
                        shared_model::interface::RolePermissionSet(permission));
                  });
                },
                [] {
                  return buildError<
                      shared_model::interface::NoRolesErrorResponse>();
                });
          });
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetAssetInfo &q) {
      using Q = QueryType<std::string, uint32_t>;
      using P = boost::tuple<int>;
      using T = concat<Q, P>;

      auto cmd = (boost::format(
                      R"(WITH has_perms AS (%s),
      perms AS (SELECT domain_id, precision FROM asset
                WHERE asset_id = :asset_id)
      SELECT domain_id, precision, perm FROM perms
      RIGHT OUTER JOIN has_perms ON TRUE
      )") % checkAccountRolePermission(Role::kReadAssets))
                     .str();
      soci::rowset<T> st = (sql_.prepare << cmd,
                            soci::use(creator_id_, "role_account_id"),
                            soci::use(q.assetId(), "asset_id"));
      auto &tuple = *st.begin();

      return validatePermissions(viewRest<P>(tuple))
          .value_or_eval([this, &tuple, &q] {
            return match_in_place(
                rebind(viewTuple<Q>(tuple)),
                [this, &q](auto &&t) {
                  return apply(t, [this, &q](auto &domain_id, auto &precision) {
                    return QueryResponseBuilder().assetResponse(
                        q.assetId(), domain_id, precision);
                  });
                },
                [] {
                  return buildError<
                      shared_model::interface::NoAssetErrorResponse>();
                });
          });
    }

    QueryResponseBuilderDone PostgresQueryExecutorVisitor::operator()(
        const shared_model::interface::GetPendingTransactions &q) {
      std::vector<shared_model::proto::Transaction> txs;
      auto interface_txs =
          pending_txs_storage_->getPendingTransactions(creator_id_);
      txs.reserve(interface_txs.size());

      std::transform(
          interface_txs.begin(),
          interface_txs.end(),
          std::back_inserter(txs),
          [](auto &tx) {
            return *(
                std::static_pointer_cast<shared_model::proto::Transaction>(tx));
          });

      // TODO 2018-08-07, rework response builder - it should take
      // interface::Transaction, igor-egorov, IR-1041
      auto response = QueryResponseBuilder().transactionsResponse(txs);
      return response;
    }

  }  // namespace ametsuchi
}  // namespace iroha
