/**
 * Copyright Soramitsu Co., Ltd. 2018 All Rights Reserved.
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

#include "ametsuchi/impl/postgres_wsv_command.hpp"

#include <boost/format.hpp>
#include "backend/protobuf/permissions.hpp"

namespace iroha {
  namespace ametsuchi {

    PostgresWsvCommand::PostgresWsvCommand(soci::session &sql)
        : sql_(sql) {}

    WsvCommandResult PostgresWsvCommand::insertRole(
        const shared_model::interface::types::RoleIdType &role_name) {
      std::cout << "insertRole(" << role_name << ")" << std::endl;
      try {
        sql_ << "INSERT INTO role(role_id) VALUES (:role_id)",
            soci::use(role_name);
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format("failed to insert role: '%s', reason: %s")
             % role_name % e.what())
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::insertAccountRole(
        const shared_model::interface::types::AccountIdType &account_id,
        const shared_model::interface::types::RoleIdType &role_name) {
      std::cout << "insertAccountRole(" << account_id << ", " << role_name
                << ")" << std::endl;
      try {
        sql_ << "INSERT INTO account_has_roles(account_id, role_id) VALUES "
                "(:account_id, :role_id)",
            soci::use(account_id), soci::use(role_name);
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format("failed to insert account role, account: '%s', "
                           "role name: '%s'")
             % account_id % role_name)
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::deleteAccountRole(
        const shared_model::interface::types::AccountIdType &account_id,
        const shared_model::interface::types::RoleIdType &role_name) {
      std::cout << "deleteAccountRole(" << account_id << ", " << role_name
                << ")" << std::endl;
      try {
        sql_ << "DELETE FROM account_has_roles WHERE account_id=:account_id AND role_id=:role_id",
            soci::use(account_id), soci::use(role_name);
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format(
                "failed to delete account role, account id: '%s', "
                    "role name: '%s'")
                % account_id % role_name)
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::insertRolePermissions(
        const shared_model::interface::types::RoleIdType &role_id,
        const shared_model::interface::RolePermissionSet &permissions) {
      auto entry = [this, &role_id](auto permission) {
        return "('" + role_id + "', '" + permission + "')";
      };

      // generate string with all permissions,
      // applying transform_func to each permission
      auto generate_perm_string = [&permissions](auto transform_func) {
        std::string s;
        permissions.iterate([&](auto perm) {
          s += transform_func(shared_model::proto::permissions::toString(perm))
              + ',';
        });
        if (s.size() > 0) {
          // remove last comma
          s.resize(s.size() - 1);
        }
        return s;
      };
      std::string params = generate_perm_string(entry);
      std::cout << "insertRolePermissions(" << role_id << ", " << params << ")" << std::endl;
      try {
        std::string query =
            "INSERT INTO role_has_permissions(role_id, permission) VALUES ";
        query += generate_perm_string(entry);
        std::cout << query << std::endl;
        sql_ << query;
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format("failed to insert role permissions, role "
                           "id: '%s', %s, permissions: [%s]")
             % role_id % e.what()
             % generate_perm_string([](auto a) { return a; }))
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::insertAccountGrantablePermission(
        const shared_model::interface::types::AccountIdType
            &permittee_account_id,
        const shared_model::interface::types::AccountIdType &account_id,
        shared_model::interface::permissions::Grantable permission) {
      std::string perm = shared_model::proto::permissions::toString(permission);
      std::cout << "insertAccountGrantablePermission(" << account_id << ", " << perm
                << ")" << std::endl;
      try {
        sql_ << "INSERT INTO "
            "account_has_grantable_permissions(permittee_account_id, "
            "account_id, permission) VALUES (:permittee_account_id, :account_id, :permission)"
            , soci::use(permittee_account_id),
            soci::use(account_id), soci::use(perm);
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format("failed to insert account grantable permission, "
                               "permittee account id: '%s', "
                               "account id: '%s', "
                               "permission: '%s',"
                               "error: %s")
                % permittee_account_id % account_id
                % shared_model::proto::permissions::toString(permission)
                % e.what())
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::deleteAccountGrantablePermission(
        const shared_model::interface::types::AccountIdType
            &permittee_account_id,
        const shared_model::interface::types::AccountIdType &account_id,
        shared_model::interface::permissions::Grantable permission) {
      std::cout << "deleteAccountGrantablePermission(" << account_id << ", " << shared_model::proto::permissions::toString(permission)
                << ")" << std::endl;
      try {
        sql_ << "DELETE FROM public.account_has_grantable_permissions WHERE "
            "permittee_account_id=:permittee_account_id AND account_id=:account_id AND permission=:permission"
            , soci::use(permittee_account_id),
            soci::use(account_id), soci::use(shared_model::proto::permissions::toString(permission));
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format("failed to delete account grantable permission, "
                               "permittee account id: '%s', "
                               "account id: '%s', "
                               "permission id: '%s'")
                % permittee_account_id % account_id
                % shared_model::proto::permissions::toString(permission))
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::insertAccount(
        const shared_model::interface::Account &account) {
      std::cout << "insertAccount(" << account.toString() << std::endl;
      try {
        sql_ << "INSERT INTO account(account_id, domain_id, quorum,"
            "data) VALUES (:id, :domain_id, :quorum, :data)",
            soci::use(account.accountId()),
            soci::use(account.domainId()),
            soci::use(account.quorum()),
            soci::use(account.jsonData());
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format("failed to insert account, "
                               "account id: '%s', "
                               "domain id: '%s', "
                               "quorum: '%d', "
                               "json_data: %s")
                % account.accountId() % account.domainId() % account.quorum()
                % account.jsonData())
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::insertAsset(
        const shared_model::interface::Asset &asset) {
      std::cout << "insertAsset(" << asset.toString() << std::endl;
      try {
        sql_ << "INSERT INTO asset(asset_id, domain_id, \"precision\", data) VALUES (:id, :domain_id, :precision, NULL)",
            soci::use(asset.assetId()),
            soci::use(asset.domainId()),
            soci::use(asset.precision());
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format("failed to insert asset, asset id: '%s', "
                               "domain id: '%s', precision: %d")
                % asset.assetId() % asset.domainId() % asset.precision())
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::upsertAccountAsset(
        const shared_model::interface::AccountAsset &asset) {

      std::cout << "upsertAccountAsset(" << asset.toString() << std::endl;
      auto balance = asset.balance().toStringRepr();
      try {
        sql_ << "INSERT INTO account_has_asset(account_id, asset_id, amount) "
            "VALUES (:account_id, :asset_id, :amount) ON CONFLICT (account_id, asset_id) DO UPDATE SET "
            "amount = EXCLUDED.amount",
            soci::use(asset.accountId()),
            soci::use(asset.assetId()),
            soci::use(balance);
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format("failed to upsert account, account id: '%s', "
                               "asset id: '%s', balance: %s, error %s")
                % asset.accountId() % asset.assetId()
                % asset.balance().toString() % e.what())
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::insertSignatory(
        const shared_model::interface::types::PubkeyType &signatory) {
      std::cout << "insertSignatory(" << signatory.toString() << std::endl;
      try {
        sql_ << "INSERT INTO signatory(public_key) VALUES (:pk) ON CONFLICT DO NOTHING;",
            soci::use(signatory.hex());
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format(
                "failed to insert signatory, signatory hex string: '%s'")
                % signatory.hex())
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::insertAccountSignatory(
        const shared_model::interface::types::AccountIdType &account_id,
        const shared_model::interface::types::PubkeyType &signatory) {
      std::cout << "insertAccountSignatory(" << account_id << ", " << signatory.toString() << std::endl;
      try {
        sql_ << "INSERT INTO account_has_signatory(account_id, public_key) VALUES (:account_id, :pk)",
            soci::use(account_id),
            soci::use(signatory.hex());
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format("failed to insert account signatory, account id: "
                               "'%s', signatory hex string: '%s")
                % account_id % signatory.hex())
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::deleteAccountSignatory(
        const shared_model::interface::types::AccountIdType &account_id,
        const shared_model::interface::types::PubkeyType &signatory) {
      std::cout << "deleteAccountSignatory(" << account_id << ", " << signatory.toString() << std::endl;
      try {
        sql_ << "DELETE FROM account_has_signatory WHERE account_id = :account_id AND public_key = :pk",
            soci::use(account_id),
            soci::use(signatory.hex());
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format("failed to delete account signatory, account id: "
                               "'%s', signatory hex string: '%s'")
                % account_id % signatory.hex())
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::deleteSignatory(
        const shared_model::interface::types::PubkeyType &signatory) {
      std::cout << "deleteSignatory(" << signatory.toString() << std::endl;
      try {
        sql_ << "DELETE FROM signatory WHERE public_key = :pk AND NOT EXISTS (SELECT 1 FROM account_has_signatory "
            "WHERE public_key = :pk) AND NOT EXISTS (SELECT 1 FROM peer WHERE public_key = :pk)",
            soci::use(signatory.hex()), soci::use(signatory.hex()), soci::use(signatory.hex());
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format(
                "failed to delete signatory, signatory hex string: '%s'")
                % signatory.hex())
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::insertPeer(
        const shared_model::interface::Peer &peer) {
      std::cout << "insertPeer(" << peer.toString() << ")" << std::endl;
      try {
        sql_ << "INSERT INTO peer(public_key, address) VALUES (:pk, :address)",
            soci::use(peer.pubkey().hex()), soci::use(peer.address());
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format(
                 "failed to insert peer, public key: '%s', address: '%s'")
             % peer.pubkey().hex() % peer.address())
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::deletePeer(
        const shared_model::interface::Peer &peer) {
      std::cout << "deletePeer(" << peer.toString() << ")" << std::endl;
      try {
        sql_ << "DELETE FROM peer WHERE public_key = :pk AND address = :address",
            soci::use(peer.pubkey().hex()), soci::use(peer.address());
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format(
                "failed to delete peer, public key: '%s', address: '%s'")
                % peer.pubkey().hex() % peer.address())
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::insertDomain(
        const shared_model::interface::Domain &domain) {
      std::cout << "insertDomain(" << domain.toString() << ")" << std::endl;
      try {
        sql_ << "INSERT INTO domain(domain_id, default_role) VALUES (:id, "
                ":role)",
            soci::use(domain.domainId()), soci::use(domain.defaultRole());
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format("failed to insert domain, domain id: '%s', "
                           "default role: '%s'")
             % domain.domainId() % domain.defaultRole())
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::updateAccount(
        const shared_model::interface::Account &account) {
      std::cout << "updateAccount(" << account.toString() << ")" << std::endl;
      try {
        sql_ << "UPDATE account SET quorum=:quorum WHERE account_id=:account_id",
            soci::use(account.quorum()), soci::use(account.accountId());
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format(
                "failed to update account, account id: '%s', quorum: '%s'")
                % account.accountId() % account.quorum())
                .str());
      }
    }

    WsvCommandResult PostgresWsvCommand::setAccountKV(
        const shared_model::interface::types::AccountIdType &account_id,
        const shared_model::interface::types::AccountIdType &creator_account_id,
        const std::string &key,
        const std::string &val) {
      std::cout << "setAccountKV(" << account_id << ", " << creator_account_id << ")" << std::endl;
      std::string json = "{" + creator_account_id + "}";
      std::string empty_json = "{}";
      std::string filled_json = "{" + creator_account_id + ", " + key + "}";
      std::string value = "\"" + val + "\"";
      try {
        sql_ << "UPDATE account SET data = jsonb_set("
            "CASE WHEN data ?:creator_account_id THEN data ELSE jsonb_set(data, :json, :empty_json) END, "
            " :filled_json, :val) WHERE account_id=:account_id",
            soci::use(creator_account_id),
            soci::use(json),
            soci::use(empty_json),
            soci::use(filled_json),
            soci::use(value),
            soci::use(account_id);
        return {};
      } catch (const std::exception &e) {
        return expected::makeError(
            (boost::format(
                "failed to set account key-value, account id: '%s', "
                    "creator account id: '%s',\n key: '%s', value: '%s'")
                % account_id % creator_account_id % key % val)
                .str());
      }
    }
  }  // namespace ametsuchi
}  // namespace iroha
