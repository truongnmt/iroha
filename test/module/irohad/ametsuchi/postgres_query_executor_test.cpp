/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ametsuchi/impl/postgres_query_executor.hpp"
#include "ametsuchi/impl/flat_file/flat_file.hpp"
#include "ametsuchi/impl/postgres_command_executor.hpp"
#include "ametsuchi/impl/postgres_wsv_query.hpp"
#include "framework/result_fixture.hpp"
#include "framework/specified_visitor.hpp"
#include "module/irohad/ametsuchi/ametsuchi_fixture.hpp"
#include "module/irohad/ametsuchi/ametsuchi_mocks.hpp"
#include "module/shared_model/builders/protobuf/test_account_builder.hpp"
#include "module/shared_model/builders/protobuf/test_asset_builder.hpp"
#include "module/shared_model/builders/protobuf/test_block_builder.hpp"
#include "module/shared_model/builders/protobuf/test_domain_builder.hpp"
#include "module/shared_model/builders/protobuf/test_peer_builder.hpp"
#include "module/shared_model/builders/protobuf/test_query_builder.hpp"
#include "module/shared_model/builders/protobuf/test_transaction_builder.hpp"
#include "utils/query_error_response_visitor.hpp"

namespace iroha {
  namespace ametsuchi {

    using namespace framework::expected;

    class QueryExecutorTest : public AmetsuchiTest {
     public:
      QueryExecutorTest() {
        domain = clone(
            TestDomainBuilder().domainId("domain").defaultRole(role).build());

        account = clone(TestAccountBuilder()
                            .domainId(domain->domainId())
                            .accountId("id@" + domain->domainId())
                            .quorum(1)
                            .jsonData(R"({"id@domain": {"key": "value"}})")
                            .build());
        role_permissions.set(
            shared_model::interface::permissions::Role::kAddMySignatory);
        grantable_permission =
            shared_model::interface::permissions::Grantable::kAddMySignatory;
        pubkey = std::make_unique<shared_model::interface::types::PubkeyType>(
            std::string('1', 32));
      }

      void SetUp() override {
        AmetsuchiTest::SetUp();
        sql = std::make_unique<soci::session>(soci::postgresql, pgopt_);

        auto factory =
            std::make_shared<shared_model::proto::ProtoCommonObjectsFactory<
                shared_model::validation::FieldValidator>>();
        query_executor = storage;
        executor = std::make_unique<PostgresCommandExecutor>(*sql);

        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().createRole(
                            role, role_permissions)),
                        true)));
        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().createDomain(
                            domain->domainId(), role)),
                        true)));
        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().createAccount(
                            "id", domain->domainId(), *pubkey)),
                        true)));
      }

      void TearDown() override {
        sql->close();
        AmetsuchiTest::TearDown();
      }

      auto executeQuery(shared_model::interface::Query &query) {
        return query_executor-> (nullptr) |
            [&query](const auto &executor) {
              return executor->validateAndExecute(query);
            };
      }

      CommandResult execute(
          const std::unique_ptr<shared_model::interface::Command> &command,
          bool do_validation = false,
          const shared_model::interface::types::AccountIdType &creator =
              "id@domain") {
        executor->doValidation(not do_validation);
        executor->setCreatorAccountId(creator);
        return boost::apply_visitor(*executor, command->get());
      }

      // TODO 2018-04-20 Alexey Chernyshov - IR-1276 - rework function with
      // CommandBuilder
      /**
       * Hepler function to build command and wrap it into
       * std::unique_ptr<>
       * @param builder command builder
       * @return command
       */
      std::unique_ptr<shared_model::interface::Command> buildCommand(
          const TestTransactionBuilder &builder) {
        return clone(builder.build().commands().front());
      }

      void addPerms(
          shared_model::interface::RolePermissionSet set,
          const shared_model::interface::types::AccountIdType account_id =
              "id@domain",
          const shared_model::interface::types::RoleIdType role_id = "perms") {
        ASSERT_TRUE(val(execute(
            buildCommand(TestTransactionBuilder().createRole(role_id, set)),
            true)));
        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().appendRole(
                            account_id, role_id)),
                        true)));
      }

      void addAllPerms(
          const shared_model::interface::types::AccountIdType account_id =
              "id@domain",
          const shared_model::interface::types::RoleIdType role_id = "all") {
        shared_model::interface::RolePermissionSet permissions;
        permissions.set();
        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().createRole(
                            role_id, permissions)),
                        true)));
        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().appendRole(
                            account_id, role_id)),
                        true)));
      }

      std::string role = "role";
      shared_model::interface::RolePermissionSet role_permissions;
      shared_model::interface::permissions::Grantable grantable_permission;
      std::unique_ptr<shared_model::interface::Account> account;
      std::unique_ptr<shared_model::interface::Domain> domain;
      std::unique_ptr<shared_model::interface::types::PubkeyType> pubkey;

      std::unique_ptr<soci::session> sql;

      std::unique_ptr<shared_model::interface::Command> command;

      std::shared_ptr<QueryExecutorFactory> query_executor;
      std::unique_ptr<CommandExecutor> executor;

      std::unique_ptr<KeyValueStorage> block_store;
    };

    class BlocksQueryExecutorTest : public QueryExecutorTest {};

    TEST_F(BlocksQueryExecutorTest, BlocksQueryExecutorTestValid) {
      addAllPerms();
      auto blocks_query = TestBlocksQueryBuilder()
                              .creatorAccountId(account->accountId())
                              .build();
      ASSERT_TRUE(query_executor->createQueryExecutor(nullptr) |
          [&blocks_query](const auto &executor) {
            return executor->validate(blocks_query);
          });
    }

    TEST_F(BlocksQueryExecutorTest, BlocksQueryExecutorTestInvalid) {
      auto blocks_query = TestBlocksQueryBuilder()
                              .creatorAccountId(account->accountId())
                              .build();
      ASSERT_FALSE(query_executor->createQueryExecutor(nullptr) |
          [&blocks_query](const auto &executor) {
            return executor->validate(blocks_query);
          });
    }

    class GetAccountExecutorTest : public QueryExecutorTest {
     public:
      void SetUp() override {
        QueryExecutorTest::SetUp();
        account2 = clone(TestAccountBuilder()
                             .domainId(domain->domainId())
                             .accountId("id2@" + domain->domainId())
                             .quorum(1)
                             .jsonData(R"({"id@domain": {"key": "value"}})")
                             .build());
        auto pubkey2 =
            std::make_unique<shared_model::interface::types::PubkeyType>(
                std::string('2', 32));
        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().createAccount(
                            "id2", domain->domainId(), *pubkey2)),
                        true)));
      }

      std::unique_ptr<shared_model::interface::Account> account2;
    };

    TEST_F(GetAccountExecutorTest, GetAccountExecutorTestValidMyAccount) {
      addPerms({shared_model::interface::permissions::Role::kGetMyAccount});
      auto query = TestQueryBuilder()
                       .createdTime(0)
                       .creatorAccountId(account->accountId())
                       .getAccount(account->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::AccountResponse>(),
            result->get());
        ASSERT_EQ(cast_resp.account().accountId(), account->accountId());
      });
    }

    TEST_F(GetAccountExecutorTest, GetAccountExecutorTestValidAllAccounts) {
      addPerms({shared_model::interface::permissions::Role::kGetAllAccounts});
      auto query = TestQueryBuilder()
                       .createdTime(0)
                       .creatorAccountId(account->accountId())
                       .getAccount(account2->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::AccountResponse>(),
            result->get());
        ASSERT_EQ(cast_resp.account().accountId(), account2->accountId());
      });
    }

    TEST_F(GetAccountExecutorTest, GetAccountExecutorTestValidDomainAccount) {
      addPerms(
          {shared_model::interface::permissions::Role::kGetDomainAccounts});
      auto query = TestQueryBuilder()
                       .createdTime(0)
                       .creatorAccountId(account->accountId())
                       .getAccount(account2->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::AccountResponse>(),
            result->get());
        ASSERT_EQ(cast_resp.account().accountId(), account2->accountId());
      });
    }

    TEST_F(GetAccountExecutorTest, GetAccountExecutorTestInvalid) {
      auto query = TestQueryBuilder()
                       .createdTime(0)
                       .creatorAccountId(account->accountId())
                       .getAccount(account2->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_THROW(
          {
            boost::apply_visitor(
                framework::SpecifiedVisitor<
                    shared_model::interface::AccountResponse>(),
                result->get());
          },
          std::runtime_error);
    }

    TEST_F(GetAccountExecutorTest, GetAccountExecutorTestInvalidNoAccount) {
      addPerms({shared_model::interface::permissions::Role::kGetAllAccounts});
      auto query = TestQueryBuilder()
                       .createdTime(0)
                       .creatorAccountId(account->accountId())
                       .getAccount("some@domain")
                       .build();
      auto result = executeQuery(query);
      ASSERT_TRUE(boost::apply_visitor(
          shared_model::interface::QueryErrorResponseChecker<
              shared_model::interface::NoAccountErrorResponse>(),
          result->get()));
    }

    class GetSignatoriesExecutorTest : public QueryExecutorTest {
     public:
      void SetUp() override {
        QueryExecutorTest::SetUp();
        account2 = clone(TestAccountBuilder()
                             .domainId(domain->domainId())
                             .accountId("id2@" + domain->domainId())
                             .quorum(1)
                             .jsonData(R"({"id@domain": {"key": "value"}})")
                             .build());
        auto pubkey2 =
            std::make_unique<shared_model::interface::types::PubkeyType>(
                std::string('2', 32));
        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().createAccount(
                            "id2", domain->domainId(), *pubkey2)),
                        true)));
      }

      std::unique_ptr<shared_model::interface::Account> account2;
    };

    TEST_F(GetSignatoriesExecutorTest,
           GetSignatoriesExecutorTestValidMyAccount) {
      addPerms({shared_model::interface::permissions::Role::kGetMySignatories});
      auto query = TestQueryBuilder()
                       .createdTime(0)
                       .creatorAccountId(account->accountId())
                       .getSignatories(account->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::SignatoriesResponse>(),
            result->get());

        ASSERT_EQ(cast_resp.keys().size(), 1);
      });
    }

    TEST_F(GetSignatoriesExecutorTest,
           GetSignatoriesExecutorTestValidAllAccounts) {
      addPerms(
          {shared_model::interface::permissions::Role::kGetAllSignatories});
      auto query = TestQueryBuilder()
                       .createdTime(0)
                       .creatorAccountId(account->accountId())
                       .getSignatories(account2->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::SignatoriesResponse>(),
            result->get());

        ASSERT_EQ(cast_resp.keys().size(), 1);
      });
    }

    TEST_F(GetSignatoriesExecutorTest,
           GetSignatoriesExecutorTestValidDomainAccount) {
      addPerms(
          {shared_model::interface::permissions::Role::kGetDomainSignatories});
      auto query = TestQueryBuilder()
                       .createdTime(0)
                       .creatorAccountId(account->accountId())
                       .getSignatories(account2->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::SignatoriesResponse>(),
            result->get());

        ASSERT_EQ(cast_resp.keys().size(), 1);
      });
    }

    TEST_F(GetSignatoriesExecutorTest, GetSignatoriesExecutorTestInvalid) {
      auto query = TestQueryBuilder()
                       .createdTime(0)
                       .creatorAccountId(account->accountId())
                       .getSignatories(account2->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_TRUE(boost::apply_visitor(
          shared_model::interface::QueryErrorResponseChecker<
              shared_model::interface::StatefulFailedErrorResponse>(),
          result->get()));
    }

    TEST_F(GetSignatoriesExecutorTest,
           GetSignatoriesExecutorTestInvalidNoAccount) {
      addPerms(
          {shared_model::interface::permissions::Role::kGetAllSignatories});
      auto query = TestQueryBuilder()
                       .createdTime(0)
                       .creatorAccountId(account->accountId())
                       .getSignatories("some@domain")
                       .build();
      auto result = executeQuery(query);
      ASSERT_TRUE(boost::apply_visitor(
          shared_model::interface::QueryErrorResponseChecker<
              shared_model::interface::NoSignatoriesErrorResponse>(),
          result->get()));
    }

    class GetAccountAssetExecutorTest : public QueryExecutorTest {
     public:
      void SetUp() override {
        QueryExecutorTest::SetUp();

        auto asset = clone(TestAccountAssetBuilder()
                               .domainId(domain->domainId())
                               .assetId(asset_id)
                               .precision(1)
                               .build());
        account2 = clone(TestAccountBuilder()
                             .domainId(domain->domainId())
                             .accountId("id2@" + domain->domainId())
                             .quorum(1)
                             .jsonData(R"({"id@domain": {"key": "value"}})")
                             .build());
        auto pubkey2 =
            std::make_unique<shared_model::interface::types::PubkeyType>(
                std::string('2', 32));
        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().createAccount(
                            "id2", domain->domainId(), *pubkey2)),
                        true)));

        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().createAsset(
                            "coin", domain->domainId(), 1)),
                        true)));

        ASSERT_TRUE(val(
            execute(buildCommand(TestTransactionBuilder()
                                     .addAssetQuantity(asset_id, "1.0")
                                     .creatorAccountId(account->accountId())),
                    true)));
        ASSERT_TRUE(val(
            execute(buildCommand(TestTransactionBuilder()
                                     .addAssetQuantity(asset_id, "1.0")
                                     .creatorAccountId(account2->accountId())),
                    true,
                    account2->accountId())));
      }

      std::unique_ptr<shared_model::interface::Account> account2;
      shared_model::interface::types::AssetIdType asset_id =
          "coin#" + domain->domainId();
    };

    TEST_F(GetAccountAssetExecutorTest,
           GetAccountAssetExecutorTestValidMyAccount) {
      addPerms({shared_model::interface::permissions::Role::kGetMyAccAst});
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountAssets(account->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::AccountAssetResponse>(),
            result->get());

        ASSERT_EQ(cast_resp.accountAssets()[0].accountId(),
                  account->accountId());
        ASSERT_EQ(cast_resp.accountAssets()[0].assetId(), asset_id);
      });
    }

    TEST_F(GetAccountAssetExecutorTest,
           GetAccountAssetExecutorTestValidAllAccounts) {
      addPerms({shared_model::interface::permissions::Role::kGetAllAccAst});
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountAssets(account2->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::AccountAssetResponse>(),
            result->get());

        ASSERT_EQ(cast_resp.accountAssets()[0].accountId(),
                  account2->accountId());
        ASSERT_EQ(cast_resp.accountAssets()[0].assetId(), asset_id);
      });
    }

    TEST_F(GetAccountAssetExecutorTest,
           GetAccountAssetExecutorTestValidDomainAccount) {
      addPerms({shared_model::interface::permissions::Role::kGetDomainAccAst});
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountAssets(account2->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::AccountAssetResponse>(),
            result->get());

        ASSERT_EQ(cast_resp.accountAssets()[0].accountId(),
                  account2->accountId());
        ASSERT_EQ(cast_resp.accountAssets()[0].assetId(), asset_id);
      });
    }

    TEST_F(GetAccountAssetExecutorTest, GetAccountAssetExecutorTestInvalid) {
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountAssets(account->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_TRUE(boost::apply_visitor(
          shared_model::interface::QueryErrorResponseChecker<
              shared_model::interface::StatefulFailedErrorResponse>(),
          result->get()));
    }

    class GetAccountDetailExecutorTest : public QueryExecutorTest {
     public:
      void SetUp() override {
        QueryExecutorTest::SetUp();

        account2 = clone(TestAccountBuilder()
                             .domainId(domain->domainId())
                             .accountId("id2@" + domain->domainId())
                             .quorum(1)
                             .jsonData("{\"id@domain\": {\"key\": \"value\", "
                                       "\"key2\": \"value2\"},"
                                       " \"id2@domain\": {\"key\": \"value\", "
                                       "\"key2\": \"value2\"}}")
                             .build());
        auto pubkey2 =
            std::make_unique<shared_model::interface::types::PubkeyType>(
                std::string('2', 32));
        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().createAccount(
                            "id2", domain->domainId(), *pubkey2)),
                        true)));

        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().createAsset(
                            "coin", domain->domainId(), 1)),
                        true)));

        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().setAccountDetail(
                            account2->accountId(), "key", "value")),
                        true,
                        account->accountId())));
        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().setAccountDetail(
                            account2->accountId(), "key2", "value2")),
                        true,
                        account->accountId())));
        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().setAccountDetail(
                            account2->accountId(), "key", "value")),
                        true,
                        account2->accountId())));
        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().setAccountDetail(
                            account2->accountId(), "key2", "value2")),
                        true,
                        account2->accountId())));
      }

      std::unique_ptr<shared_model::interface::Account> account2;
    };

    TEST_F(GetAccountDetailExecutorTest,
           GetAccountDetailExecutorTestValidMyAccount) {
      addPerms({shared_model::interface::permissions::Role::kGetMyAccDetail});
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountDetail(account->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::AccountDetailResponse>(),
            result->get());

        ASSERT_EQ(cast_resp.detail(), "{}");
      });
    }

    TEST_F(GetAccountDetailExecutorTest,
           GetAccountDetailExecutorTestValidAllAccounts) {
      addPerms({shared_model::interface::permissions::Role::kGetAllAccDetail});
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountDetail(account2->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::AccountDetailResponse>(),
            result->get());

        ASSERT_EQ(cast_resp.detail(), account2->jsonData());
      });
    }

    TEST_F(GetAccountDetailExecutorTest,
           GetAccountDetailExecutorTestValidDomainAccount) {
      addPerms(
          {shared_model::interface::permissions::Role::kGetDomainAccDetail});
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountDetail(account2->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::AccountDetailResponse>(),
            result->get());

        ASSERT_EQ(cast_resp.detail(), account2->jsonData());
      });
    }

    TEST_F(GetAccountDetailExecutorTest, GetAccountDetailExecutorTestInvalid) {
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountDetail(account2->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_TRUE(boost::apply_visitor(
          shared_model::interface::QueryErrorResponseChecker<
              shared_model::interface::StatefulFailedErrorResponse>(),
          result->get()));
    }

    TEST_F(GetAccountDetailExecutorTest,
           GetAccountDetailExecutorTestInvalidNoAccount) {
      addPerms({shared_model::interface::permissions::Role::kGetAllAccDetail});
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountDetail("some@domain")
                       .build();
      auto result = executeQuery(query);
      ASSERT_TRUE(boost::apply_visitor(
          shared_model::interface::QueryErrorResponseChecker<
              shared_model::interface::NoAccountDetailErrorResponse>(),
          result->get()));
    }

    TEST_F(GetAccountDetailExecutorTest, GetAccountDetailExecutorTestValidKey) {
      addPerms({shared_model::interface::permissions::Role::kGetAllAccDetail});
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountDetail(account2->accountId(), "key")
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::AccountDetailResponse>(),
            result->get());

        ASSERT_EQ(cast_resp.detail(),
                  "{ \"id@domain\" : {\"key\" : \"value\"}, \"id2@domain\" : "
                  "{\"key\" : \"value\"} }");
      });
    }

    TEST_F(GetAccountDetailExecutorTest,
           GetAccountDetailExecutorTestValidWriter) {
      addPerms({shared_model::interface::permissions::Role::kGetAllAccDetail});
      auto query =
          TestQueryBuilder()
              .creatorAccountId(account->accountId())
              .getAccountDetail(account2->accountId(), "", account->accountId())
              .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::AccountDetailResponse>(),
            result->get());

        ASSERT_EQ(
            cast_resp.detail(),
            "{\"id@domain\" : {\"key\": \"value\", \"key2\": \"value2\"}}");
      });
    }

    TEST_F(GetAccountDetailExecutorTest,
           GetAccountDetailExecutorTestValidKeyWriter) {
      addPerms({shared_model::interface::permissions::Role::kGetAllAccDetail});
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountDetail(
                           account2->accountId(), "key", account->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::AccountDetailResponse>(),
            result->get());

        ASSERT_EQ(cast_resp.detail(),
                  "{\"id@domain\" : {\"key\" : \"value\"}}");
      });
    }

    class GetRolesExecutorTest : public QueryExecutorTest {
     public:
      void SetUp() override {
        QueryExecutorTest::SetUp();
      }
    };

    TEST_F(GetRolesExecutorTest, GetRolesExecutorTestValid) {
      addPerms({shared_model::interface::permissions::Role::kGetRoles});
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getRoles()
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp =
            boost::apply_visitor(framework::SpecifiedVisitor<
                                     shared_model::interface::RolesResponse>(),
                                 result->get());

        ASSERT_EQ(cast_resp.roles().size(), 2);
        ASSERT_EQ(cast_resp.roles()[0], "role");
        ASSERT_EQ(cast_resp.roles()[1], "perms");
      });
    }

    TEST_F(GetRolesExecutorTest, GetRolesExecutorTestInvalid) {
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getRoles()
                       .build();
      auto result = executeQuery(query);
      ASSERT_TRUE(boost::apply_visitor(
          shared_model::interface::QueryErrorResponseChecker<
              shared_model::interface::StatefulFailedErrorResponse>(),
          result->get()));
    }

    class GetRolePermsExecutorTest : public QueryExecutorTest {
     public:
      void SetUp() override {
        QueryExecutorTest::SetUp();
      }
    };

    TEST_F(GetRolePermsExecutorTest, GetRolePermsExecutorTestValid) {
      addPerms({shared_model::interface::permissions::Role::kGetRoles});
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getRolePermissions("perms")
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::RolePermissionsResponse>(),
            result->get());

        ASSERT_TRUE(cast_resp.rolePermissions().test(
            shared_model::interface::permissions::Role::kGetRoles));
      });
    }

    TEST_F(GetRolePermsExecutorTest, GetRolePermsExecutorTestInvalidNoRole) {
      addPerms({shared_model::interface::permissions::Role::kGetRoles});
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getRolePermissions("some")
                       .build();
      auto result = executeQuery(query);
      ASSERT_TRUE(boost::apply_visitor(
          shared_model::interface::QueryErrorResponseChecker<
              shared_model::interface::NoRolesErrorResponse>(),
          result->get()));
    }

    TEST_F(GetRolePermsExecutorTest, GetRolePermsExecutorTestInvalid) {
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getRolePermissions("role")
                       .build();
      auto result = executeQuery(query);
      ASSERT_TRUE(boost::apply_visitor(
          shared_model::interface::QueryErrorResponseChecker<
              shared_model::interface::StatefulFailedErrorResponse>(),
          result->get()));
    }

    class GetAssetInfoExecutorTest : public QueryExecutorTest {
     public:
      void SetUp() override {
        QueryExecutorTest::SetUp();
      }

      void createAsset() {
        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().createAsset(
                            "coin", domain->domainId(), 1)),
                        true)));
      }
      const std::string asset_id = "coin#domain";
    };

    TEST_F(GetAssetInfoExecutorTest, GetAssetInfoExecutorTestValid) {
      addPerms({shared_model::interface::permissions::Role::kReadAssets});
      createAsset();
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAssetInfo(asset_id)
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp =
            boost::apply_visitor(framework::SpecifiedVisitor<
                                     shared_model::interface::AssetResponse>(),
                                 result->get());

        ASSERT_EQ(cast_resp.asset().assetId(), asset_id);
        ASSERT_EQ(cast_resp.asset().domainId(), domain->domainId());
        ASSERT_EQ(cast_resp.asset().precision(), 1);
      });
    }

    TEST_F(GetAssetInfoExecutorTest, GetAssetInfoExecutorTestInvalidNoAsset) {
      addPerms({shared_model::interface::permissions::Role::kReadAssets});
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAssetInfo("some#domain")
                       .build();
      auto result = executeQuery(query);
      ASSERT_TRUE(boost::apply_visitor(
          shared_model::interface::QueryErrorResponseChecker<
              shared_model::interface::NoAssetErrorResponse>(),
          result->get()));
    }

    TEST_F(GetAssetInfoExecutorTest, GetAssetInfoExecutorTestInvalid) {
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAssetInfo(asset_id)
                       .build();
      auto result = executeQuery(query);
      ASSERT_TRUE(boost::apply_visitor(
          shared_model::interface::QueryErrorResponseChecker<
              shared_model::interface::StatefulFailedErrorResponse>(),
          result->get()));
    }

    class GetTransactionsExecutorTest : public QueryExecutorTest {
     public:
      void SetUp() override {
        QueryExecutorTest::SetUp();
        std::string block_store_dir = "/tmp/block_store";
        auto block_converter =
            std::make_shared<shared_model::proto::ProtoBlockJsonConverter>();
        auto factory =
            std::make_shared<shared_model::proto::ProtoCommonObjectsFactory<
                shared_model::validation::FieldValidator>>();
        auto block_store = FlatFile::create(block_store_dir);
        ASSERT_TRUE(block_store);
        this->block_store = std::move(block_store.get());

        account2 = clone(TestAccountBuilder()
                             .domainId(domain->domainId())
                             .accountId("id2@" + domain->domainId())
                             .quorum(1)
                             .jsonData(R"({"id@domain": {"key": "value"}})")
                             .build());
        auto pubkey2 =
            std::make_unique<shared_model::interface::types::PubkeyType>(
                std::string('2', 32));
        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().createAccount(
                            "id2", domain->domainId(), *pubkey2)),
                        true)));
        ASSERT_TRUE(
            val(execute(buildCommand(TestTransactionBuilder().createAsset(
                            "coin", domain->domainId(), 1)),
                        true)));
      }

      /**
       * Apply block to given storage
       * @tparam S storage type
       * @param storage storage object
       * @param block to apply
       */
      template <typename S>
      void apply(S &&storage, const shared_model::interface::Block &block) {
        std::unique_ptr<MutableStorage> ms;
        auto storageResult = storage->createMutableStorage();
        storageResult.match(
            [&](iroha::expected::Value<std::unique_ptr<MutableStorage>>
                    &_storage) { ms = std::move(_storage.value); },
            [](iroha::expected::Error<std::string> &error) {
              FAIL() << "MutableStorage: " << error.error;
            });
        ms->apply(block,
                  [](const auto &, auto &, const auto &) { return true; });
        storage->commit(std::move(ms));
      }

      void commitBlocks() {
        auto zero_string = std::string(32, '0');
        auto fake_hash = shared_model::crypto::Hash(zero_string);
        auto fake_pubkey = shared_model::crypto::PublicKey(zero_string);

        auto tx1 = TestTransactionBuilder()
                       .creatorAccountId(account->accountId())
                       .createRole("user", {})
                       .build();

        auto tx2 = TestTransactionBuilder()
                       .creatorAccountId(account->accountId())
                       .addAssetQuantity(asset_id, "2.0")
                       .transferAsset(account->accountId(),
                                      account2->accountId(),
                                      asset_id,
                                      "",
                                      "1.0")
                       .build();

        auto tx3 = TestTransactionBuilder()
                       .creatorAccountId(account2->accountId())
                       .transferAsset(account->accountId(),
                                      account2->accountId(),
                                      asset_id,
                                      "",
                                      "1.0")
                       .build();

        auto block1 =
            TestBlockBuilder()
                .transactions(std::vector<shared_model::proto::Transaction>({
                    tx1,
                    tx2,
                    TestTransactionBuilder()
                        .creatorAccountId(account2->accountId())
                        .createRole("user2", {})
                        .build(),
                }))
                .height(1)
                .prevHash(fake_hash)
                .build();

        apply(storage, block1);

        auto block2 =
            TestBlockBuilder()
                .transactions(std::vector<shared_model::proto::Transaction>(
                    {tx3,
                     TestTransactionBuilder()
                         .creatorAccountId(account->accountId())
                         .createRole("user3", {})
                         .build()

                    }))
                .height(2)
                .prevHash(block1.hash())
                .build();

        apply(storage, block2);

        hash1 = tx1.hash();
        hash2 = tx2.hash();
        hash3 = tx3.hash();
      }

      const std::string asset_id = "coin#domain";
      std::unique_ptr<shared_model::interface::Account> account2;
      shared_model::crypto::Hash hash1;
      shared_model::crypto::Hash hash2;
      shared_model::crypto::Hash hash3;
    };

    class GetAccountTransactionsExecutorTest
        : public GetTransactionsExecutorTest {};

    TEST_F(GetAccountTransactionsExecutorTest,
           GetAccountTransactionsExecutorTestValidMyAcc) {
      addPerms({shared_model::interface::permissions::Role::kGetMyAccTxs});

      commitBlocks();

      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountTransactions(account->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::TransactionsResponse>(),
            result->get());
        ASSERT_EQ(cast_resp.transactions().size(), 3);
        for (const auto &tx : cast_resp.transactions()) {
          EXPECT_EQ(account->accountId(), tx.creatorAccountId());
        }
      });
    }

    TEST_F(GetAccountTransactionsExecutorTest,
           GetAccountTransactionsExecutorTestValidAllAcc) {
      addPerms({shared_model::interface::permissions::Role::kGetAllAccTxs});

      commitBlocks();

      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountTransactions(account2->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::TransactionsResponse>(),
            result->get());
        ASSERT_EQ(cast_resp.transactions().size(), 2);
        for (const auto &tx : cast_resp.transactions()) {
          EXPECT_EQ(account2->accountId(), tx.creatorAccountId());
        }
      });
    }

    TEST_F(GetAccountTransactionsExecutorTest,
           GetAccountTransactionsExecutorTestValidDomainAcc) {
      addPerms({shared_model::interface::permissions::Role::kGetDomainAccTxs});

      commitBlocks();

      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountTransactions(account2->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::TransactionsResponse>(),
            result->get());
        ASSERT_EQ(cast_resp.transactions().size(), 2);
        for (const auto &tx : cast_resp.transactions()) {
          EXPECT_EQ(account2->accountId(), tx.creatorAccountId());
        }
      });
    }

    TEST_F(GetAccountTransactionsExecutorTest,
           GetAccountTransactionsExecutorTestInvalid) {
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountTransactions(account->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_TRUE(boost::apply_visitor(
          shared_model::interface::QueryErrorResponseChecker<
              shared_model::interface::StatefulFailedErrorResponse>(),
          result->get()));
    }

    class GetTransactionsHashExecutorTest : public GetTransactionsExecutorTest {
    };

    TEST_F(GetTransactionsHashExecutorTest,
           GetTransactionsHashExecutorTestValidAllAcc) {
      addPerms({shared_model::interface::permissions::Role::kGetAllTxs});

      commitBlocks();

      std::vector<decltype(hash1)> hashes;
      hashes.push_back(hash1);
      hashes.push_back(hash2);
      hashes.push_back(hash3);

      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getTransactions(hashes)
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::TransactionsResponse>(),
            result->get());
        ASSERT_EQ(cast_resp.transactions().size(), 3);
        ASSERT_EQ(cast_resp.transactions()[0].hash(), hash1);
        ASSERT_EQ(cast_resp.transactions()[1].hash(), hash2);
        ASSERT_EQ(cast_resp.transactions()[2].hash(), hash3);
      });
    }

    TEST_F(GetTransactionsHashExecutorTest,
           GetTransactionsHashExecutorTestValidMyAcc) {
      addPerms({shared_model::interface::permissions::Role::kGetMyTxs});

      commitBlocks();

      std::vector<decltype(hash1)> hashes;
      hashes.push_back(hash1);
      hashes.push_back(hash2);
      hashes.push_back(hash3);

      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getTransactions(hashes)
                       .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::TransactionsResponse>(),
            result->get());
        ASSERT_EQ(cast_resp.transactions().size(), 2);
        ASSERT_EQ(cast_resp.transactions()[0].hash(), hash1);
        ASSERT_EQ(cast_resp.transactions()[1].hash(), hash2);
      });
    }

    class GetAccountAssetTransactionsExecutorTest
        : public GetTransactionsExecutorTest {};

    TEST_F(GetAccountAssetTransactionsExecutorTest,
           GetAccountAssetTransactionsExecutorTestValidMyAcc) {
      addPerms({shared_model::interface::permissions::Role::kGetMyAccAstTxs});

      commitBlocks();

      auto query =
          TestQueryBuilder()
              .creatorAccountId(account->accountId())
              .getAccountAssetTransactions(account->accountId(), asset_id)
              .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::TransactionsResponse>(),
            result->get());
        ASSERT_EQ(cast_resp.transactions().size(), 2);
        ASSERT_EQ(cast_resp.transactions()[0].hash(), hash2);
        ASSERT_EQ(cast_resp.transactions()[1].hash(), hash3);
      });
    }

    TEST_F(GetAccountAssetTransactionsExecutorTest,
           GetAccountAssetTransactionsExecutorTestValidAllAcc) {
      addPerms({shared_model::interface::permissions::Role::kGetAllAccAstTxs});

      commitBlocks();

      auto query =
          TestQueryBuilder()
              .creatorAccountId(account->accountId())
              .getAccountAssetTransactions(account2->accountId(), asset_id)
              .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::TransactionsResponse>(),
            result->get());
        ASSERT_EQ(cast_resp.transactions().size(), 2);
        ASSERT_EQ(cast_resp.transactions()[0].hash(), hash2);
        ASSERT_EQ(cast_resp.transactions()[1].hash(), hash3);
      });
    }

    TEST_F(GetAccountAssetTransactionsExecutorTest,
           GetAccountAssetTransactionsExecutorTestValidDomainAcc) {
      addPerms(
          {shared_model::interface::permissions::Role::kGetDomainAccAstTxs});

      commitBlocks();

      auto query =
          TestQueryBuilder()
              .creatorAccountId(account->accountId())
              .getAccountAssetTransactions(account2->accountId(), asset_id)
              .build();
      auto result = executeQuery(query);
      ASSERT_NO_THROW({
        const auto &cast_resp = boost::apply_visitor(
            framework::SpecifiedVisitor<
                shared_model::interface::TransactionsResponse>(),
            result->get());
        ASSERT_EQ(cast_resp.transactions().size(), 2);
        ASSERT_EQ(cast_resp.transactions()[0].hash(), hash2);
        ASSERT_EQ(cast_resp.transactions()[1].hash(), hash3);
      });
    }

    TEST_F(GetAccountAssetTransactionsExecutorTest,
           GetAccountAssetTransactionsExecutorTestInvalid) {
      auto query = TestQueryBuilder()
                       .creatorAccountId(account->accountId())
                       .getAccountTransactions(account->accountId())
                       .build();
      auto result = executeQuery(query);
      ASSERT_TRUE(boost::apply_visitor(
          shared_model::interface::QueryErrorResponseChecker<
              shared_model::interface::StatefulFailedErrorResponse>(),
          result->get()));
    }
  }  // namespace ametsuchi
}  // namespace iroha
