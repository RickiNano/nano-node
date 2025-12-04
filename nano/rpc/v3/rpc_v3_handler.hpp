#pragma once

#include <nano/rpc/rpc_version_handler.hpp>

#include <boost/json.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace nano
{
class node;
class node_rpc_config;

/**
 * RPC v3 handler implementing the version 3 API.
 * Uses Boost.JSON for fast JSON parsing and response building.
 * Provides action-based dispatch to individual endpoint handlers.
 */
class rpc_v3_handler : public rpc_version_handler
{
public:
	rpc_v3_handler (nano::node &, nano::node_rpc_config const &);

	/**
	 * Process a v3 RPC request.
	 * Parses JSON, extracts action, dispatches to appropriate handler.
	 */
	void process_request (
	std::string const & body,
	std::function<void (std::string const &)> response) override;

	/**
	 * Returns the API version number (3).
	 */
	int version () const override
	{
		return 3;
	}

private:
	nano::node & node;
	nano::node_rpc_config const & config;

	// Action dispatch map
	using handler_func = std::function<boost::json::object (boost::json::object const &)>;
	std::unordered_map<std::string, handler_func> action_handlers;

	/**
	 * Register all endpoint handlers during construction.
	 */
	void register_handlers ();

	// Endpoint handler methods

	// Tier 1 - Info/Query endpoints
	boost::json::object handle_version (boost::json::object const & request);
	boost::json::object handle_account_balance (boost::json::object const & request);
	boost::json::object handle_account_info (boost::json::object const & request);
	boost::json::object handle_block_info (boost::json::object const & request);
	boost::json::object handle_block_count (boost::json::object const & request);
	boost::json::object handle_available_supply (boost::json::object const & request);
	boost::json::object handle_peers (boost::json::object const & request);
	boost::json::object handle_uptime (boost::json::object const & request);
	boost::json::object handle_validate_account_number (boost::json::object const & request);
	boost::json::object handle_account_representative (boost::json::object const & request);

	// Tier 2 - Lists/Bulk endpoints
	boost::json::object handle_accounts_balances (boost::json::object const & request);
	boost::json::object handle_blocks_info (boost::json::object const & request);

	// Tier 3 - State-Modifying endpoints
	boost::json::object handle_block_create (boost::json::object const & request);
};
}
