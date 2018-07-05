#
# Copyright Soramitsu Co., Ltd. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0
#

add_library(pending_txs_storage
    txs_storage.cpp
    tx_batches_extractor.cpp
    )

target_link_libraries(pending_txs_storage
    mst_processor
    shared_model_cryptography
    )
