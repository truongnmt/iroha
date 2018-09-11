/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "torii/impl/command_service_transport_grpc.hpp"

#include <iterator>

#include "backend/protobuf/transaction.hpp"
#include "backend/protobuf/transaction_responses/proto_tx_response.hpp"
#include "builders/protobuf/transaction_sequence_builder.hpp"
#include "common/timeout.hpp"
#include "cryptography/default_hash_provider.hpp"
#include "validators/default_validator.hpp"

namespace torii {

  CommandServiceTransportGrpc::CommandServiceTransportGrpc(
      std::shared_ptr<CommandService> command_service,
      std::chrono::milliseconds initial_timeout,
      std::chrono::milliseconds nonfinal_timeout)
      : command_service_(std::move(command_service)),
        initial_timeout_(initial_timeout),
        nonfinal_timeout_(nonfinal_timeout),
        log_(logger::log("CommandServiceTransportGrpc")) {}

  grpc::Status CommandServiceTransportGrpc::Torii(
      grpc::ServerContext *context,
      const iroha::protocol::Transaction *request,
      google::protobuf::Empty *response) {
    iroha::protocol::TxList single_tx_list;
    *single_tx_list.add_transactions() = *request;
    return ListTorii(context, &single_tx_list, response);
  }

  grpc::Status CommandServiceTransportGrpc::ListTorii(
      grpc::ServerContext *context,
      const iroha::protocol::TxList *request,
      google::protobuf::Empty *response) {
    auto tx_list_builder = shared_model::proto::TransportBuilder<
        shared_model::interface::TransactionSequence,
        shared_model::validation::DefaultUnsignedTransactionsValidator>();

    tx_list_builder.build(*request).match(
        [this](
            iroha::expected::Value<shared_model::interface::TransactionSequence>
                &tx_sequence) {
          this->command_service_->handleTransactionList(tx_sequence.value);
        },
        [this, request](auto &error) {
          auto &txs = request->transactions();
          if (txs.empty()) {
            log_->warn("Received no transactions. Skipping");
            return;
          }
          using HashType = shared_model::crypto::Hash;

          std::vector<HashType> hashes;
          std::transform(
              txs.begin(),
              txs.end(),
              std::back_inserter(hashes),
              [](const auto &tx) {
                return shared_model::crypto::DefaultHashProvider::makeHash(
                    shared_model::proto::makeBlob(tx.payload()));
              });
          this->command_service_->handleTransactionListError(hashes,
                                                             error.error);
        });
    return grpc::Status::OK;
  }

  grpc::Status CommandServiceTransportGrpc::Status(
      grpc::ServerContext *context,
      const iroha::protocol::TxStatusRequest *request,
      iroha::protocol::ToriiResponse *response) {
    auto status = command_service_->getStatus(
        shared_model::crypto::Hash(request->tx_hash()));
    response->set_tx_hash(
        shared_model::crypto::toBinaryString(status->transactionHash()));
    response->set_error_message(std::move(status->errorMessage()));
    response->set_tx_status(
        static_cast<iroha::protocol::TxStatus>(status->get().which()));
    return grpc::Status::OK;
  }

  namespace {
    void handleEvents(rxcpp::composite_subscription &subscription,
                      rxcpp::schedulers::run_loop &run_loop) {
      while (subscription.is_subscribed() or not run_loop.empty()) {
        run_loop.dispatch();
      }
    }
  }  // namespace

  grpc::Status CommandServiceTransportGrpc::StatusStream(
      grpc::ServerContext *context,
      const iroha::protocol::TxStatusRequest *request,
      grpc::ServerWriter<iroha::protocol::ToriiResponse> *response_writer) {
    rxcpp::schedulers::run_loop rl;

    auto current_thread =
        rxcpp::observe_on_one_worker(rxcpp::schedulers::make_run_loop(rl));

    rxcpp::composite_subscription subscription;

    auto hash = shared_model::crypto::Hash(request->tx_hash());

    static auto client_id_format = boost::format("Peer: '%s', %s");
    std::string client_id =
        (client_id_format % context->peer() % hash.toString()).str();

    command_service_
        ->getStatusStream(hash)
        // convert to transport objects
        .map([&](auto response) {
          log_->debug("mapped {}, {}", response->toString(), client_id);
          return std::static_pointer_cast<
                     shared_model::proto::TransactionResponse>(response)
              ->getTransport();
        })
        // set a corresponding observable timeout based on status value
        .lift<iroha::protocol::ToriiResponse>(
            iroha::makeTimeout<iroha::protocol::ToriiResponse>(
                [&](const auto &response) {
                  return response.tx_status()
                          == iroha::protocol::TxStatus::NOT_RECEIVED
                      ? initial_timeout_
                      : nonfinal_timeout_;
                },
                current_thread))
        // complete the observable if client is disconnected
        .take_while([=](const auto &) {
          auto is_cancelled = context->IsCancelled();
          if (is_cancelled) {
            log_->debug("client unsubscribed, {}", client_id);
          }
          return not is_cancelled;
        })
        .subscribe(subscription,
                   [&](iroha::protocol::ToriiResponse response) {
                     if (response_writer->Write(response)) {
                       log_->debug("status written, {}", client_id);
                     }
                   },
                   [&](std::exception_ptr ep) {
                     log_->debug("processing timeout, {}", client_id);
                   },
                   [&] { log_->debug("stream done, {}", client_id); });

    // run loop while subscription is active or there are pending events in
    // the queue
    handleEvents(subscription, rl);

    log_->debug("status stream done, {}", client_id);
    return grpc::Status::OK;
  }
}  // namespace torii
