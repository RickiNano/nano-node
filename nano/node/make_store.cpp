#include <nano/lib/logging.hpp>
#include <nano/node/make_store.hpp>
#include <nano/store/lmdb/lmdb.hpp>
#include <nano/store/rocksdb/rocksdb.hpp>

std::unique_ptr<nano::store::component> nano::make_store (nano::logger & logger, std::filesystem::path const & path, nano::ledger_constants & constants, bool read_only, bool add_db_postfix, nano::node_config const & node_config, nano::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a, nano::lmdb_config const & lmdb_config_a, bool backup_before_upgrade, bool force_use_write_queue)
{
	const char * unit_testing_backend = std::getenv ("BACKEND");
	if (unit_testing_backend != nullptr)
	{
		// make_store was called from unit test. Set database backend accordingly
		std::cout << "DB backend: " << unit_testing_backend << std::endl;
		if (std::strcmp (unit_testing_backend, "rocksdb"))
		{
			std::cout << "DB : " << unit_testing_backend << std::endl;
			return std::make_unique<nano::store::rocksdb::component> (logger, add_db_postfix ? path / "rocksdb" : path, constants, node_config.rocksdb_config, read_only, force_use_write_queue);
		}
		if (std::strcmp (unit_testing_backend, "lmdb"))
		{
			std::cout << "DB : " << unit_testing_backend << std::endl;
			return std::make_unique<nano::store::lmdb::component> (logger, add_db_postfix ? path / "data.ldb" : path, constants, node_config.diagnostics_config.txn_tracking, block_processor_batch_max_time_a, node_config.lmdb_config, backup_before_upgrade);
		}
		debug_assert (false);
	}

	std::cout << "**** UNIT TEST NOT DETECTED ****" << unit_testing_backend << std::endl;
	debug_assert (false);

	if (node_config.database_backend == nano::database_backend::lmdb)
	{
		return std::make_unique<nano::store::lmdb::component> (logger, add_db_postfix ? path / "data.ldb" : path, constants, txn_tracking_config_a, block_processor_batch_max_time_a, lmdb_config_a, backup_before_upgrade);
	}
	else if (node_config.database_backend == nano::database_backend::rocksdb)
	{
		return std::make_unique<nano::store::rocksdb::component> (logger, add_db_postfix ? path / "rocksdb" : path, constants, node_config.rocksdb_config, read_only, force_use_write_queue);
	}
	else if (node_config.database_backend == nano::database_backend::automatic)
	{
		bool lmdb_ledger_found = std::filesystem::exists (path / "data.ldb");
		bool rocks_ledger_found = std::filesystem::exists (path / "rocksdb");
		if (lmdb_ledger_found && rocks_ledger_found)
		{
			logger.warn (nano::log::type::ledger, "Multiple ledgers were found! Using RocksDb ledger");
			return std::make_unique<nano::store::rocksdb::component> (logger, add_db_postfix ? path / "rocksdb" : path, constants, node_config.rocksdb_config, read_only, force_use_write_queue);
		}
		else if (lmdb_ledger_found)
		{
			logger.info (nano::log::type::ledger, "Found LMDB ledger");
			return std::make_unique<nano::store::lmdb::component> (logger, add_db_postfix ? path / "data.ldb" : path, constants, txn_tracking_config_a, block_processor_batch_max_time_a, lmdb_config_a, backup_before_upgrade);
		}
		else if (rocks_ledger_found)
		{
			logger.info (nano::log::type::ledger, "Found RocksDb ledger");
			return std::make_unique<nano::store::rocksdb::component> (logger, add_db_postfix ? path / "rocksdb" : path, constants, node_config.rocksdb_config, read_only, force_use_write_queue);
		}
		else if (!lmdb_ledger_found && !rocks_ledger_found)
		{
			logger.info (nano::log::type::ledger, "No ledger found. Creating new RocksDb ledger");
			return std::make_unique<nano::store::rocksdb::component> (logger, add_db_postfix ? path / "rocksdb" : path, constants, node_config.rocksdb_config, read_only, force_use_write_queue);
		}
	}

	debug_assert (false);
}