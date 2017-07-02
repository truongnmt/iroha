/*
Copyright Soramitsu Co., Ltd. 2016 All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <network/network_api.h>
#include <consensus/connection/service.hpp>
#include <consensus/consensus_service_stub.hpp>
#include <main/application.hpp>
#include <network/peer_communication_stub.hpp>
#include <ordering/ordering_service_stub.hpp>
#include <torii/processor/client_processor_stub.hpp>
#include <torii/torii_stub.hpp>
#include <validation/chain/validator_stub.hpp>
#include <validation/stateful/stub_validator.hpp>
#include <validation/stateless/validator_stub.hpp>

#include <dao/dao.hpp>

#include "server_runner.hpp"

int main(int argc, char* argv[]) {
  /*
    connection::api::CommandService commandService;
    connection::api::QueryService queryService;
    consensus::connection::SumeragiService sumeragiService;
    ordering::connection::OrderingService orderingService;

    ServerRunner serverRunner("0.0.0.0", 50051, {
        &commandService,
        &queryService,
        &sumeragiService,
        &orderingService
    });
  */
  iroha::Irohad irohad;
  iroha::validation::StatelessValidatorStub stateless_validator;
  iroha::validation::ValidatorStub stateful_validator;
  iroha::validation::ChainValidatorStub chain_validator;
  iroha::ordering::OrderingServiceStub ordering_service;
  iroha::consensus::ConsensusServiceStub consensus_service;
  iroha::network::PeerCommunicationServiceStub peer_communication_service(
      irohad.ametsuchi, stateful_validator, chain_validator, ordering_service,
      consensus_service, irohad.cryptoProvider);
  iroha::torii::ClientProcessorStub client_processor(
      stateless_validator, peer_communication_service, irohad.cryptoProvider);
  iroha::torii::ToriiStub torii(client_processor);

  iroha::dao::GetBlocks query;
  query.from = 32;
  query.to = 64;
  torii.get_query({}, query);

  return 0;
}
