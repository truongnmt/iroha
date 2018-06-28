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

#include "ametsuchi/impl/postgres_wsv_query.hpp"
#include "backend/protobuf/from_old.hpp"
#include "backend/protobuf/permissions.hpp"

namespace iroha {
  namespace ametsuchi {

    using shared_model::interface::types::AccountIdType;
    using shared_model::interface::types::AssetIdType;
    using shared_model::interface::types::DomainIdType;
    using shared_model::interface::types::JsonType;
    using shared_model::interface::types::PubkeyType;
    using shared_model::interface::types::RoleIdType;

    const std::string kRoleId = "role_id";
    const char *kAccountNotFound = "Account {} not found";
    const std::string kPublicKey = "public_key";
    const std::string kAssetId = "asset_id";
    const std::string kAccountId = "account_id";
    const std::string kDomainId = "domain_id";

    PostgresWsvQuery::PostgresWsvQuery(soci::session &sql)
        : sql_(sql),
          log_(logger::log("PostgresWsvQuery")) {}

    PostgresWsvQuery::PostgresWsvQuery(
        std::unique_ptr<soci::session> sql_ptr)
        : sql_ptr_(std::move(sql_ptr)),
          sql_(*sql_ptr_),
          log_(logger::log("PostgresWsvQuery")) {}

    bool PostgresWsvQuery::hasAccountGrantablePermission(
        const AccountIdType &permitee_account_id,
        const AccountIdType &account_id,
        shared_model::interface::permissions::Grantable permission) {
      if (permission == shared_model::interface::permissions::Grantable::COUNT) {
        auto perm = shared_model::proto::permissions::toString(permission);
        return false;
      }
//      std::cout << "hasAccountGrantablePermission(" << permitee_account_id << ", " << account_id << ", " << shared_model::proto::permissions::toString(permission) << ")" << std::endl;
      int size;
      auto perm = shared_model::proto::permissions::toString(permission);
      sql_ << "SELECT count(*) FROM account_has_grantable_permissions WHERE "
          "permittee_account_id = :permittee_account_id AND account_id = :account_id "
          " AND permission = :permission ", soci::into(size), soci::use(permitee_account_id), soci::use(account_id), soci::use(perm);
      return size == 1;
    }

    boost::optional<std::vector<RoleIdType>> PostgresWsvQuery::getAccountRoles(
        const AccountIdType &account_id) {
//      std::cout << "getAccountRoles(" << account_id << ")" << std::endl;
      int size;
      sql_ << "SELECT count(*) FROM account_has_roles WHERE account_id = :account_id", soci::into(size), soci::use(account_id);

      if (size == 0) {
        return std::vector<RoleIdType>();
      }

      std::vector<RoleIdType> roles(size);
      sql_ << "SELECT role_id FROM account_has_roles WHERE account_id = :account_id", soci::into(roles), soci::use(account_id);

      return roles;
    }

    boost::optional<shared_model::interface::RolePermissionSet>
    PostgresWsvQuery::getRolePermissions(const RoleIdType &role_name) {
//      std::cout << "getRolePermissions(" << role_name << ")" << std::endl;
      shared_model::interface::RolePermissionSet set;

      int size;
      sql_ << "SELECT count(permission) FROM role_has_permissions WHERE role_id = :role_name", soci::into(size), soci::use(role_name);

      if (size == 0) {
        return set;
      }

      std::vector<std::string> perms(size);
      sql_ << "SELECT permission FROM role_has_permissions WHERE role_id = :role_name", soci::into(perms), soci::use(role_name);

      for (const auto &perm: perms) {
        set.set(shared_model::interface::permissions::fromOldR(perm));
      }

      return set;
    }

    boost::optional<std::vector<RoleIdType>> PostgresWsvQuery::getRoles() {
      soci::rowset<RoleIdType> roles = (sql_.prepare << "SELECT role_id FROM role");
      std::vector<RoleIdType> result;
      for (const auto &role: roles) {
        result.push_back(role);
      }
      return boost::make_optional(result);
    }

    boost::optional<std::shared_ptr<shared_model::interface::Account>>
    PostgresWsvQuery::getAccount(const AccountIdType &account_id) {
      boost::optional<std::string> domain_id, data;
      boost::optional<shared_model::interface::types::QuorumType> quorum;
//      std::cout << "getAccount(" << account_id << ")" << std::endl;
      sql_ << "SELECT domain_id, quorum, data FROM account WHERE account_id = "
              ":account_id",
          soci::into(domain_id), soci::into(quorum), soci::into(data),
          soci::use(account_id);

      if (not domain_id) {
        return boost::none;
      }

      return fromResult(
          makeAccount(account_id, domain_id.get(), quorum.get(), data.get()));
    }

    boost::optional<std::string> PostgresWsvQuery::getAccountDetail(
        const std::string &account_id) {
//      std::cout << "getAccountDetail(" << account_id << ")" << std::endl;
      boost::optional<std::string> detail;

      sql_ << "SELECT data FROM account WHERE account_id = :account_id", soci::into(detail), soci::use(account_id);

      return detail;
    }

    boost::optional<std::vector<PubkeyType>> PostgresWsvQuery::getSignatories(
        const AccountIdType &account_id) {
//      std::cout << "getSignatories(" << account_id << ")" << std::endl;
      int size;
      sql_ << "SELECT count(*) FROM account_has_signatory WHERE account_id = "
              ":account_id",
          soci::into(size), soci::use(account_id);

      std::vector<PubkeyType> pubkeys;
      if (size == 0) {
        return pubkeys;
      }

      std::vector<std::string> rows(size);

      sql_ << "SELECT public_key FROM account_has_signatory WHERE account_id = "
              ":account_id",
          soci::into(rows), soci::use(account_id);

      std::for_each(rows.begin(), rows.end(), [&](const auto &pk) {
        pubkeys.push_back(shared_model::crypto::PublicKey(
            shared_model::crypto::Blob::fromHexString(pk)));
      });

      return boost::make_optional(pubkeys);
    }

    boost::optional<std::shared_ptr<shared_model::interface::Asset>>
    PostgresWsvQuery::getAsset(const AssetIdType &asset_id) {

      boost::optional<std::string> domain_id, data;
      boost::optional<int32_t> precision;
      boost::optional<shared_model::interface::types::QuorumType> quorum;
//      std::cout << "getAsset(" << asset_id << ")" << std::endl;
      sql_ << "SELECT domain_id, precision FROM asset WHERE asset_id = "
          ":account_id",
          soci::into(domain_id), soci::into(precision),
          soci::use(asset_id);

      if (not domain_id) {
        return boost::none;
      }

      return fromResult(
          makeAsset(asset_id, domain_id.get(), precision.get()));
    }

    boost::optional<
        std::vector<std::shared_ptr<shared_model::interface::AccountAsset>>>
    PostgresWsvQuery::getAccountAssets(const AccountIdType &account_id) {
//      std::cout << "getAccountAssets(" << account_id << ")" << std::endl;
      int size;
      sql_ << "SELECT count(*) FROM account_has_asset WHERE account_id = :account_id", soci::into(size), soci::use(account_id);

      if (size == 0) {
        return std::vector<std::shared_ptr<shared_model::interface::AccountAsset>>();
      }

      std::vector<std::string> asset(size), balance(size);
      sql_ << "SELECT asset_id, amount FROM account_has_asset WHERE account_id = :account_id", soci::into(asset), soci::into(balance), soci::use(account_id);

      std::vector<std::shared_ptr<shared_model::interface::AccountAsset>> assets;

      for (int i = 0; i < asset.size(); i++) {
        auto result = fromResult(makeAccountAsset(account_id, asset.at(i), balance.at(i)));
        if (result) {
          std::shared_ptr<shared_model::interface::AccountAsset> ass = result.get();
          assets.push_back(ass);
        }
      }
      return boost::make_optional(assets);
    }

    boost::optional<std::shared_ptr<shared_model::interface::AccountAsset>>
    PostgresWsvQuery::getAccountAsset(const AccountIdType &account_id,
                                      const AssetIdType &asset_id) {
//      std::cout << "getAccountAsset(" << account_id << ", " << asset_id << ")" << std::endl;
      boost::optional<std::string> amount;
      sql_ << "SELECT amount FROM account_has_asset WHERE account_id = :account_id AND asset_id = :asset_id", soci::into(amount), soci::use(account_id), soci::use(asset_id);

      if (not amount) {
        return boost::none;
      }

      return fromResult(makeAccountAsset(account_id, asset_id, amount.get()));
    }

    boost::optional<std::shared_ptr<shared_model::interface::Domain>>
    PostgresWsvQuery::getDomain(const DomainIdType &domain_id) {
//      std::cout << "getDomain(" << domain_id << ")" << std::endl;
      boost::optional<std::string> role;
      sql_ << "SELECT default_role FROM domain WHERE domain_id = :id LIMIT 1",
          soci::into(role), soci::use(domain_id);

      if (not role) {
        return boost::none;
      }

      return fromResult(makeDomain(domain_id, role.get()));
    }

    boost::optional<std::vector<std::shared_ptr<shared_model::interface::Peer>>>
    PostgresWsvQuery::getPeers() {
//      std::cout << "getPeers()" << std::endl;
      soci::rowset<soci::row> rows =
          (sql_.prepare << "SELECT public_key, address FROM peer");
      std::vector<std::shared_ptr<shared_model::interface::Peer>> peers;

      auto results = transform<
          shared_model::builder::BuilderResult<shared_model::interface::Peer>>(
          rows, makePeer);
      for (auto &r : results) {
        r.match(
            [&](expected::Value<std::shared_ptr<shared_model::interface::Peer>>
                    &v) { peers.push_back(v.value); },
            [&](expected::Error<std::shared_ptr<std::string>> &e) {
              log_->info(*e.error);
            });
      }
      return peers;
    }
  }  // namespace ametsuchi
}  // namespace iroha
