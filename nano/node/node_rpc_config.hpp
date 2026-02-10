#pragma once

#include <nano/lib/rpcconfig.hpp>

#include <boost/json/fwd.hpp>

#include <string>

namespace nano
{
class tomlconfig;
class rpc_child_process_config final
{
public:
	bool enable{ false };
	std::string rpc_path{ get_default_rpc_filepath () };
};

class node_rpc_config final
{
public:
	nano::error serialize_toml (nano::tomlconfig & toml) const;
	nano::error deserialize_toml (nano::tomlconfig & toml);

	bool enable_sign_hash{ false };
	nano::rpc_child_process_config child_process;

	// Used in tests to ensure requests are modified in specific cases
	void set_request_callback (std::function<void (boost::json::object const &)>);
	std::function<void (boost::json::object const &)> request_callback;
};
}
