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
class rpc_config;

/**
 * RPC v3 handler implementing the version 3 API.
 * Uses Boost.JSON for fast JSON parsing and response building.
 * Provides action-based dispatch to individual endpoint handlers.
 */
class rpc_v3_handler : public rpc_version_handler
{
public:
	rpc_v3_handler (nano::node &, nano::node_rpc_config const &, nano::rpc_config const &, std::function<void ()> stop_callback = [] () { });

	/**
	 * Process a v3 RPC request.
	 * Parses JSON, extracts action, dispatches to appropriate handler.
	 */
	void process_request (
	std::string const & body,
	std::function<void (std::string const &)> response) override;

	/**
	 * Process a v3 RPC request with action extracted from URL path.
	 * Used for URL-based routing (e.g., /api/uptime -> action="uptime").
	 * Parses JSON body, injects action, dispatches to appropriate handler.
	 */
	void process_request_with_path_action (
	std::string const & action,
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
	nano::rpc_config const & rpc_config;
	std::function<void ()> stop_callback;

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
	boost::json::object handle_account_block_count (boost::json::object const & request);
	boost::json::object handle_account_weight (boost::json::object const & request);
	boost::json::object handle_block_info (boost::json::object const & request);
	boost::json::object handle_block_account (boost::json::object const & request);
	boost::json::object handle_block_count (boost::json::object const & request);
	boost::json::object handle_available_supply (boost::json::object const & request);
	boost::json::object handle_peers (boost::json::object const & request);
	boost::json::object handle_uptime (boost::json::object const & request);
	boost::json::object handle_validate_account_number (boost::json::object const & request);
	boost::json::object handle_account_representative (boost::json::object const & request);
	boost::json::object handle_receivable (boost::json::object const & request);
	boost::json::object handle_receivable_exists (boost::json::object const & request);
	boost::json::object handle_representatives (boost::json::object const & request);
	boost::json::object handle_representatives_online (boost::json::object const & request);
	boost::json::object handle_online_reps (boost::json::object const & request);
	boost::json::object handle_account_get (boost::json::object const & request);
	boost::json::object handle_account_key (boost::json::object const & request);
	boost::json::object handle_account_count (boost::json::object const & request);
	boost::json::object handle_account_history (boost::json::object const & request);
	boost::json::object handle_frontiers (boost::json::object const & request);
	boost::json::object handle_ledger (boost::json::object const & request);
	boost::json::object handle_accounts_frontiers (boost::json::object const & request);
	boost::json::object handle_block_hash (boost::json::object const & request);
	boost::json::object handle_blocks (boost::json::object const & request);
	boost::json::object handle_chain (boost::json::object const & request);
	boost::json::object handle_node_id (boost::json::object const & request);
	boost::json::object handle_active_difficulty (boost::json::object const & request);
	boost::json::object handle_delegators_count (boost::json::object const & request);
	boost::json::object handle_delegators (boost::json::object const & request);
	boost::json::object handle_confirmation_quorum (boost::json::object const & request);
	boost::json::object handle_confirmation_info (boost::json::object const & request);
	boost::json::object handle_unchecked (boost::json::object const & request);
	boost::json::object handle_unchecked_get (boost::json::object const & request);
	boost::json::object handle_unchecked_keys (boost::json::object const & request);
	boost::json::object handle_pruned_exists (boost::json::object const & request);
	boost::json::object handle_pending (boost::json::object const & request);
	boost::json::object handle_pending_exists (boost::json::object const & request);

	// Tier 2 - Lists/Bulk endpoints
	boost::json::object handle_accounts_balances (boost::json::object const & request);
	boost::json::object handle_blocks_info (boost::json::object const & request);

	// Utility/Conversion endpoints
	boost::json::object handle_nano_to_raw (boost::json::object const & request);
	boost::json::object handle_raw_to_nano (boost::json::object const & request);
	boost::json::object handle_key_create (boost::json::object const & request);
	boost::json::object handle_key_expand (boost::json::object const & request);

	// Tier 3 - State-Modifying endpoints
	boost::json::object handle_block_create (boost::json::object const & request);
	boost::json::object handle_process (boost::json::object const & request);
	boost::json::object handle_sign (boost::json::object const & request);

	// Work endpoints
	boost::json::object handle_work_generate (boost::json::object const & request);
	boost::json::object handle_work_validate (boost::json::object const & request);

	// Stats/Monitoring endpoints
	boost::json::object handle_stats (boost::json::object const & request);
	boost::json::object handle_telemetry (boost::json::object const & request);
	boost::json::object handle_confirmation_active (boost::json::object const & request);
	boost::json::object handle_confirmation_history (boost::json::object const & request);

	// Control endpoints
	boost::json::object handle_stop (boost::json::object const & request);
};
}
