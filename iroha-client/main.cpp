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

#include <endpoint.pb.h>
#include <gflags/gflags.h>
#include <chrono>
#include <cstdlib>
#include <numeric>
#include <thread>
#include "builders/protobuf/transaction.hpp"
#include "crypto/keys_manager_impl.hpp"
#include "torii/command_client.hpp"

namespace shared_model {
  namespace interface {
    class Transaction;
    class Query;
  }  // namespace interface
}  // namespace shared_model

DEFINE_int32(tx_num, 5000, "Total number of transactions");
DEFINE_int32(tx_rate, 500, "Transaction rate");
DEFINE_string(keypair, "../../example/admin@test", "Path to admin keypair");
DEFINE_string(ip, "51.15.244.195", "IP address of iroha torii");
DEFINE_int32(port, 50055, "Port of iroha torii");

using namespace torii;

auto getTxStatus(CommandSyncClient client, std::string tx_hash) {
  iroha::protocol::TxStatusRequest statusRequest;
  iroha::protocol::ToriiResponse toriiResponse;
  statusRequest.set_tx_hash(tx_hash);

  auto getAnswer = [&]() {
    return client.Status(statusRequest, toriiResponse);
  };
  decltype(getAnswer()) answer;
  auto tx_status = client.Status(statusRequest, toriiResponse);

  return toriiResponse.tx_status();
}

auto createInitTransaction(const shared_model::crypto::Keypair &keypair) {
  return shared_model::proto::TransactionBuilder()
      .creatorAccountId("admin@test")
      .createdTime(iroha::time::now())
      .addAssetQuantity("coin#test", std::to_string(FLAGS_tx_num))
      .quorum(1)
      .build()
      .signAndAddSignature(keypair)
      .finish();
}

auto createTransferTransaction(const shared_model::crypto::Keypair &keypair) {
  return shared_model::proto::TransactionBuilder()
      .creatorAccountId("admin@test")
      .createdTime(iroha::time::now())
      .transferAsset("admin@test",
                     "test@test",
                     "coin#test",
                     std::to_string(rand() % 100000),
                     "1")
      .quorum(1)
      .build()
      .signAndAddSignature(keypair)
      .finish();
}



int main(int argc, char *argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  gflags::ShutDownCommandLineFlags();
  std::cout << "Transaction number " << FLAGS_tx_num << std::endl;
  std::cout << "Transaction rate " << FLAGS_tx_rate << std::endl;

  // Intialize transactions
  // Ip and port
  // CliClient client("ip", 23);
  CommandSyncClient client(FLAGS_ip, FLAGS_port);

  iroha::KeysManagerImpl keysManager(FLAGS_keypair);
  auto keypair = keysManager.loadKeys();
  if (!keypair) {
    std::cout << "Cannot load keypair " << std::endl;
    return EXIT_FAILURE;
  }
  auto t1 = std::chrono::steady_clock::now();
  auto begin = std::chrono::steady_clock::now();
  std::unordered_map<std::string, decltype(begin)> status_map;

  auto init_tx = createInitTransaction(keypair.get());

  auto tx_stat = client.Torii(init_tx.getTransport());
  iroha::protocol::TxStatus status;
  do {
    status = getTxStatus(client,
                         shared_model::crypto::toBinaryString(init_tx.hash()));
  } while (status == iroha::protocol::STATELESS_VALIDATION_SUCCESS
           or status == iroha::protocol::STATEFUL_VALIDATION_SUCCESS);
  if (status != iroha::protocol::COMMITTED) {
    // Failed transaction
    std::cout << "Transaction failed with status " << status << std::endl;
    return EXIT_FAILURE;
  }
  auto end = std::chrono::steady_clock::now();
  // Time measures
  auto val = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
                 .count();
  int tx_count = 0;
  int failed = 0;
  int success = 0;
  std::vector<double> latencies;
  std::vector<int> tps;
  latencies.push_back(val / 1000);
  auto tx_begin = std::chrono::steady_clock::now();
  while (tx_count < FLAGS_tx_num) {
    // Send tx_rate transactions to Iroha
    begin = std::chrono::steady_clock::now();
    for (int i = 0; i < FLAGS_tx_rate; ++i) {
      auto tx = createTransferTransaction(keypair.get());
      auto tx_stat = CommandSyncClient(FLAGS_ip, FLAGS_port).Torii(tx.getTransport());
      status_map.insert(
          std::make_pair(shared_model::crypto::toBinaryString(tx.hash()),
                         std::chrono::steady_clock::now()));
    }
    tx_count += FLAGS_tx_rate;
    // Wait for 1 second
    end = std::chrono::steady_clock::now();
    val = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
              .count();
    std::cout << " Sending " << FLAGS_tx_rate << " transactions "
              << ", total sent " << tx_count << "Time " << val / 1000
              << std::endl;

    // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    int prev_commited = success;
    auto stat_begin = std::chrono::steady_clock::now();
    for (auto it = status_map.begin(); it != status_map.end();) {
      status = getTxStatus(client, it->first);
      if (status == iroha::protocol::COMMITTED) {
        // Transaction commited, remove it
        end = std::chrono::steady_clock::now();
        begin = it->second;
        val = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
                  .count();
        latencies.push_back(val / 1000);
        it = status_map.erase(it);
        success++;
      } else if (status == iroha::protocol::STATEFUL_VALIDATION_FAILED
                 or status == iroha::protocol::STATELESS_VALIDATION_FAILED) {
        // Transaction failed
        it = status_map.erase(it);
        failed++;
        std::cout << "Transaction failed :( " << std::endl;
      }
      if (it == status_map.end()) {
        break;
      }
      ++it;
    }
    auto stat_end = std::chrono::steady_clock::now();
    val = std::chrono::duration_cast<std::chrono::milliseconds>(stat_end
                                                                - stat_begin)
              .count();
    std::cout << "Transactions commited: " << success << ". Now commited "
              << success - prev_commited << " Time " << val / 1000 << std::endl;
    tps.push_back(success - prev_commited);
  }

  bool first_time = true;
  while (!status_map.empty()) {
    int prev_commited = success;
    for (auto it = status_map.begin(); it != status_map.end();) {
      status = getTxStatus(client, it->first);
      if (status == iroha::protocol::COMMITTED) {
        // Transaction commited, remove it
        end = std::chrono::steady_clock::now();
        val = std::chrono::duration_cast<std::chrono::milliseconds>(
                  end - it->second)
                  .count();
        latencies.push_back(val / 1000);
        it = status_map.erase(it);
        success++;
      } else if (status == iroha::protocol::STATEFUL_VALIDATION_FAILED
                 or status == iroha::protocol::STATELESS_VALIDATION_FAILED) {
        // Transaction failed
        it = status_map.erase(it);
        failed++;
        std::cout << "Transaction failed :( " << std::endl;
      }
      if (it == status_map.end()) {
        break;
      }
      it++;
    }
    std::cout << "Transactions commited: " << success << ". Now commited "
              << success - prev_commited << std::endl;
    if (first_time) {
      tps.push_back(success - prev_commited);
      first_time = false;
    }
  }

  // Calculate latencies
  std::cout << "Max latency "
            << *std::max_element(latencies.begin(), latencies.end())
            << std::endl;
  std::cout << "Min latency "
            << *std::min_element(latencies.begin(), latencies.end())
            << std::endl;

  double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
  double mean = sum / latencies.size();
  for (auto v : latencies) {
    // std::cout << " " << v;
  }
  std::cout << std::endl;
  std::cout << "Avg latency " << mean << std::endl;
  std::cout << "Failed transactions " << failed << std::endl;
  end = std::chrono::steady_clock::now();
  val = std::chrono::duration_cast<std::chrono::milliseconds>(end - tx_begin)
            .count();
  sum = std::accumulate(tps.begin(), tps.end(), 0.0);
  mean = sum / tps.size();
  std::cout << "Avg tps " << mean << std::endl;
}
