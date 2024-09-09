#pragma once

#include <nano/lib/logging.hpp>
#include <nano/node/nodeconfig.hpp>

#include <chrono>

namespace nano
{
class ledger_constants;
class node_config;
}

namespace nano::store
{
class component;
}

namespace nano
{
std::unique_ptr<nano::store::component> make_store (nano::logger &, std::filesystem::path const & path, nano::ledger_constants & constants, bool open_read_only = false, bool add_db_postfix = true, nano::node_config const & node_config = nano::node_config{}, nano::txn_tracking_config const & txn_tracking_config_a = nano::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), nano::lmdb_config const & lmdb_config_a = nano::lmdb_config{}, bool backup_before_upgrade = false, bool force_use_write_queue = false);
}
