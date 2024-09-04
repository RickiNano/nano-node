#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/threading.hpp>

#include <thread>

namespace nano
{
class tomlconfig;

/** Configuration options for RocksDB */
class rocksdb_config final
{
public:
	nano::error serialize_toml (nano::tomlconfig &) const;
	nano::error deserialize_toml (nano::tomlconfig &);

	/** To use RocksDB in tests make sure the environment variable TEST_USE_ROCKSDB=1 is set */
	static bool using_rocksdb_in_tests ();

	uint8_t memory_multiplier{ 2 };
	unsigned io_threads{ std::max (nano::hardware_concurrency () / 2, 1u) };
};
}
