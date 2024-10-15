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

	bool enable{ false };
	unsigned io_threads{ std::max (nano::hardware_concurrency () / 2, 1u) };
	long read_cache{ 32 };
	long write_cache{ 64 };
};
}
