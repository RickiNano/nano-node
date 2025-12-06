#include <nano/rpc/v3/rpc_v3_handler.hpp>

#include <nano/lib/block_type.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/stats_sinks.hpp>
#include <nano/lib/version.hpp>
#include <nano/lib/work.hpp>
#include <nano/lib/work_version.hpp>
#include <nano/node/election.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/messages.hpp>
#include <nano/node/node.hpp>
#include <nano/node/node_rpc_config.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/rpc/v3/error_codes.hpp>
#include <nano/rpc/v3/response_builder.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>

#include <boost/format.hpp>
#include <boost/json.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <sstream>

namespace nano
{
rpc_v3_handler::rpc_v3_handler (nano::node & node_a, nano::node_rpc_config const & config_a, nano::rpc_config const & rpc_config_a, std::function<void ()> stop_callback_a) :
	node (node_a),
	config (config_a),
	rpc_config (rpc_config_a),
	stop_callback (stop_callback_a)
{
	register_handlers ();
}

void rpc_v3_handler::register_handlers ()
{
	// Register endpoint handlers using lambda captures

	// Tier 1 - Info/Query
	action_handlers["version"] = [this] (auto const & req) { return handle_version (req); };
	action_handlers["account_balance"] = [this] (auto const & req) { return handle_account_balance (req); };
	action_handlers["account_info"] = [this] (auto const & req) { return handle_account_info (req); };
	action_handlers["account_block_count"] = [this] (auto const & req) { return handle_account_block_count (req); };
	action_handlers["account_weight"] = [this] (auto const & req) { return handle_account_weight (req); };
	action_handlers["account_representative"] = [this] (auto const & req) { return handle_account_representative (req); };
	action_handlers["block_info"] = [this] (auto const & req) { return handle_block_info (req); };
	action_handlers["block_account"] = [this] (auto const & req) { return handle_block_account (req); };
	action_handlers["block_count"] = [this] (auto const & req) { return handle_block_count (req); };
	action_handlers["available_supply"] = [this] (auto const & req) { return handle_available_supply (req); };
	action_handlers["peers"] = [this] (auto const & req) { return handle_peers (req); };
	action_handlers["uptime"] = [this] (auto const & req) { return handle_uptime (req); };
	action_handlers["validate_account_number"] = [this] (auto const & req) { return handle_validate_account_number (req); };
	action_handlers["receivable"] = [this] (auto const & req) { return handle_receivable (req); };
	action_handlers["receivable_exists"] = [this] (auto const & req) { return handle_receivable_exists (req); };
	action_handlers["representatives"] = [this] (auto const & req) { return handle_representatives (req); };
	action_handlers["representatives_online"] = [this] (auto const & req) { return handle_representatives_online (req); };
	action_handlers["online_weight_info"] = [this] (auto const & req) { return handle_online_reps (req); };
	action_handlers["account_get"] = [this] (auto const & req) { return handle_account_get (req); };
	action_handlers["account_key"] = [this] (auto const & req) { return handle_account_key (req); };
	action_handlers["account_count"] = [this] (auto const & req) { return handle_account_count (req); };
	action_handlers["account_history"] = [this] (auto const & req) { return handle_account_history (req); };
	action_handlers["frontiers"] = [this] (auto const & req) { return handle_frontiers (req); };
	action_handlers["ledger"] = [this] (auto const & req) { return handle_ledger (req); };
	action_handlers["accounts_frontiers"] = [this] (auto const & req) { return handle_accounts_frontiers (req); };
	action_handlers["block_hash"] = [this] (auto const & req) { return handle_block_hash (req); };
	action_handlers["blocks"] = [this] (auto const & req) { return handle_blocks (req); };
	action_handlers["chain"] = [this] (auto const & req) { return handle_chain (req); };
	action_handlers["node_id"] = [this] (auto const & req) { return handle_node_id (req); };
	action_handlers["active_difficulty"] = [this] (auto const & req) { return handle_active_difficulty (req); };
	action_handlers["delegators_count"] = [this] (auto const & req) { return handle_delegators_count (req); };
	action_handlers["delegators"] = [this] (auto const & req) { return handle_delegators (req); };
	action_handlers["confirmation_quorum"] = [this] (auto const & req) { return handle_confirmation_quorum (req); };
	action_handlers["confirmation_info"] = [this] (auto const & req) { return handle_confirmation_info (req); };
	action_handlers["unchecked"] = [this] (auto const & req) { return handle_unchecked (req); };
	action_handlers["unchecked_get"] = [this] (auto const & req) { return handle_unchecked_get (req); };
	action_handlers["unchecked_keys"] = [this] (auto const & req) { return handle_unchecked_keys (req); };
	action_handlers["pruned_exists"] = [this] (auto const & req) { return handle_pruned_exists (req); };
	action_handlers["pending"] = [this] (auto const & req) { return handle_pending (req); };
	action_handlers["pending_exists"] = [this] (auto const & req) { return handle_pending_exists (req); };

	// Tier 2 - Lists/Bulk
	action_handlers["accounts_balances"] = [this] (auto const & req) { return handle_accounts_balances (req); };
	action_handlers["blocks_info"] = [this] (auto const & req) { return handle_blocks_info (req); };

	// Utility/Conversion
	action_handlers["nano_to_raw"] = [this] (auto const & req) { return handle_nano_to_raw (req); };
	action_handlers["raw_to_nano"] = [this] (auto const & req) { return handle_raw_to_nano (req); };
	action_handlers["key_create"] = [this] (auto const & req) { return handle_key_create (req); };
	action_handlers["key_expand"] = [this] (auto const & req) { return handle_key_expand (req); };

	// Tier 3 - State-Modifying
	action_handlers["block_create"] = [this] (auto const & req) { return handle_block_create (req); };
	action_handlers["process"] = [this] (auto const & req) { return handle_process (req); };
	action_handlers["sign"] = [this] (auto const & req) { return handle_sign (req); };

	// Work endpoints
	action_handlers["work_generate"] = [this] (auto const & req) { return handle_work_generate (req); };
	action_handlers["work_validate"] = [this] (auto const & req) { return handle_work_validate (req); };

	// Stats/Monitoring endpoints
	action_handlers["stats"] = [this] (auto const & req) { return handle_stats (req); };
	action_handlers["telemetry"] = [this] (auto const & req) { return handle_telemetry (req); };
	action_handlers["confirmation_active"] = [this] (auto const & req) { return handle_confirmation_active (req); };
	action_handlers["confirmation_history"] = [this] (auto const & req) { return handle_confirmation_history (req); };

	// Control endpoints
	action_handlers["stop"] = [this] (auto const & req) { return handle_stop (req); };
}

void rpc_v3_handler::process_request (
std::string const & body,
std::function<void (std::string const &)> response)
{
	// V3 API uses RESTful URL-based actions only.
	// This method is deprecated for HTTP requests - use /api/{action} endpoints instead.
	// Return error directing users to the correct API format.
	auto error_response = nano::rpc::v3::response_builder::error (
		"V3 API requires URL-based actions. Use /api/{action} endpoints (e.g., POST /api/uptime) instead of including action in the request body.");
	response (nano::rpc::v3::response_builder::serialize (error_response));
}

void rpc_v3_handler::process_request_with_path_action (
std::string const & action,
std::string const & body,
std::function<void (std::string const &)> response)
{
	try
	{
		// Parse JSON request body (may be empty or contain parameters)
		boost::json::object request_obj;

		if (!body.empty ())
		{
			boost::json::value request_value = boost::json::parse (body);

			// If body is an object, use it; otherwise start fresh
			if (request_value.is_object ())
			{
				request_obj = request_value.as_object ();
			}
		}

		// Remove any "action" field from the body - for v3, action comes only from URL
		if (request_obj.contains ("action"))
		{
			request_obj.erase ("action");
		}

		// Set the action from URL path (the only source of action for v3)
		request_obj["action"] = action;

		// Look up handler for this action
		auto handler_it = action_handlers.find (action);
		if (handler_it == action_handlers.end ())
		{
			auto error_response = nano::rpc::v3::response_builder::error ("Unknown action: " + action);
			response (nano::rpc::v3::response_builder::serialize (error_response));
			return;
		}

		// Call the handler
		auto result = handler_it->second (request_obj);
		response (nano::rpc::v3::response_builder::serialize (result));
	}
	catch (boost::system::system_error const & e)
	{
		// JSON parsing error
		auto error_response = nano::rpc::v3::response_builder::error (std::string ("JSON parsing error: ") + e.what ());
		response (nano::rpc::v3::response_builder::serialize (error_response));
	}
	catch (std::exception const & e)
	{
		// General error
		auto error_response = nano::rpc::v3::response_builder::error (std::string ("Internal error: ") + e.what ());
		response (nano::rpc::v3::response_builder::serialize (error_response));
	}
	catch (...)
	{
		// Catch-all for any non-standard exceptions
		auto error_response = nano::rpc::v3::response_builder::error ("Unknown error occurred");
		response (nano::rpc::v3::response_builder::serialize (error_response));
	}
}

boost::json::object rpc_v3_handler::handle_version (boost::json::object const & request)
{
	// Build version information matching legacy format
	boost::json::object data;
	data["rpc_version"] = "1";
	data["store_version"] = std::to_string (node.store_version ());
	data["protocol_version"] = std::to_string (node.network_params.network.protocol_version);
	data["node_vendor"] = boost::str (boost::format ("Nano %1%") % NANO_VERSION_STRING);
	data["store_vendor"] = node.store.vendor_get ();
	data["network"] = node.network_params.network.get_current_network_as_string ();
	data["network_identifier"] = node.network_params.ledger.genesis->hash ().to_string ();
	data["build_info"] = BUILD_INFO;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_account_balance (boost::json::object const & request)
{
	// Validate required field: account
	if (!request.contains ("account"))
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	auto const & account_value = request.at ("account");
	if (!account_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	std::string account_text = std::string (account_value.as_string ());
	nano::account account;

	// Decode account address
	if (account.decode_account (account_text))
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	// Get optional parameter: include_only_confirmed (default: true)
	bool include_only_confirmed = true;
	if (request.contains ("include_only_confirmed"))
	{
		auto const & confirmed_value = request.at ("include_only_confirmed");
		if (confirmed_value.is_bool ())
		{
			include_only_confirmed = confirmed_value.as_bool ();
		}
	}

	// Get balance and pending from node
	auto balance_pending = node.balance_pending (account, include_only_confirmed);

	// Build response data
	boost::json::object data;
	data["balance"] = balance_pending.first.convert_to<std::string> ();
	data["pending"] = balance_pending.second.convert_to<std::string> ();
	data["receivable"] = balance_pending.second.convert_to<std::string> (); // Alias for pending

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_account_info (boost::json::object const & request)
{
	// Validate required field: account
	if (!request.contains ("account"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: account");
	}

	auto const & account_value = request.at ("account");
	if (!account_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'account' must be a string");
	}

	std::string account_text = std::string (account_value.as_string ());
	nano::account account;

	if (account.decode_account (account_text))
	{
		return nano::rpc::v3::response_builder::error ("Invalid account format");
	}

	// Get account info from ledger
	auto transaction = node.store.tx_begin_read ();
	nano::account_info info;

	if (!node.store.account.get (transaction, account, info))
	{
		return nano::rpc::v3::response_builder::error ("Account not found");
	}

	// Get confirmation height info
	nano::confirmation_height_info conf_info;
	node.store.confirmation_height.get (transaction, account, conf_info);

	// Build response data
	boost::json::object data;
	data["frontier"] = info.head.to_string ();
	data["open_block"] = info.open_block.to_string ();
	data["representative_block"] = info.representative.to_string ();
	data["balance"] = nano::amount{ info.balance.number () }.to_string_dec ();
	data["modified_timestamp"] = std::to_string (info.modified);
	data["block_count"] = std::to_string (info.block_count);
	data["account_version"] = std::to_string (static_cast<uint8_t> (info.epoch ()));
	data["confirmation_height"] = std::to_string (conf_info.height);
	data["confirmation_frontier"] = conf_info.frontier.to_string ();

	// Include representative if requested
	bool representative = false;
	if (request.contains ("representative") && request.at ("representative").is_bool ())
	{
		representative = request.at ("representative").as_bool ();
	}
	if (representative)
	{
		data["representative"] = info.representative.to_account ();
	}

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_block_info (boost::json::object const & request)
{
	// Validate required field: hash
	if (!request.contains ("hash"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: hash");
	}

	auto const & hash_value = request.at ("hash");
	if (!hash_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'hash' must be a string");
	}

	std::string hash_text = std::string (hash_value.as_string ());
	nano::block_hash hash;

	if (hash.decode_hex (hash_text))
	{
		return nano::rpc::v3::response_builder::error ("Invalid block hash");
	}

	// Get block from ledger
	auto transaction = node.ledger.tx_begin_read ();
	auto block = node.ledger.any.block_get (transaction, hash);

	if (!block)
	{
		return nano::rpc::v3::response_builder::error ("Block not found");
	}

	// Get block account
	auto account = node.ledger.any.block_account (transaction, hash);

	// Build response data
	boost::json::object data;
	data["block_account"] = account.value_or (0).to_account ();
	data["contents"] = block->to_json ();

	// Get additional info
	auto amount = node.ledger.any.block_amount (transaction, hash);
	if (amount)
	{
		data["amount"] = nano::amount{ amount->number () }.to_string_dec ();
	}

	auto balance = node.ledger.any.block_balance (transaction, hash);
	if (balance)
	{
		data["balance"] = nano::amount{ balance->number () }.to_string_dec ();
	}

	// Check if confirmed
	auto height = node.ledger.any.block_height (transaction, hash);
	if (height > 0)
	{
		data["height"] = std::to_string (height);
		data["confirmed"] = true;
	}
	else
	{
		data["confirmed"] = false;
	}

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_block_count (boost::json::object const & request)
{
	boost::json::object data;
	data["count"] = std::to_string (node.ledger.block_count ());
	data["unchecked"] = std::to_string (node.unchecked.count ());
	data["cemented"] = std::to_string (node.ledger.cemented_count ());

	if (node.flags.enable_pruning)
	{
		data["full"] = std::to_string (node.ledger.block_count () - node.ledger.pruned_count ());
		data["pruned"] = std::to_string (node.ledger.pruned_count ());
	}

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_available_supply (boost::json::object const & request)
{
	auto genesis_balance = node.balance (node.network_params.ledger.genesis->account ());
	auto landing_balance = node.balance (nano::account ("059F68AAB29DE0D3A27443625C7EA9CDDB6517A8B76FE37727EF6A4D76832AD5"));
	auto faucet_balance = node.balance (nano::account ("8E319CE6F3025E5B2DF66DA7AB1467FE48F1679C13DD43BFDB29FA2E9FC40D3B"));
	auto burned_balance = (node.balance_pending (nano::account{}, false)).second;

	auto available = nano::uint128_t{ node.network_params.ledger.genesis_amount } - genesis_balance - landing_balance - faucet_balance - burned_balance;

	boost::json::object data;
	data["available"] = available.convert_to<std::string> ();

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_peers (boost::json::object const & request)
{
	boost::json::object peers_obj;

	auto peers_list = node.network.list ();
	for (auto const & peer : peers_list)
	{
		std::stringstream text;
		text << peer->get_peering_endpoint ();
		peers_obj[text.str ()] = std::to_string (peer->get_network_version ());
	}

	boost::json::object data;
	data["peers"] = peers_obj;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_uptime (boost::json::object const & request)
{
	boost::json::object data;
	data["seconds"] = std::to_string (std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - node.startup_time).count ());

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_validate_account_number (boost::json::object const & request)
{
	// Validate required field: account
	if (!request.contains ("account"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: account");
	}

	auto const & account_value = request.at ("account");
	if (!account_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'account' must be a string");
	}

	std::string account_text = std::string (account_value.as_string ());
	nano::account account;

	bool valid = !account.decode_account (account_text);

	boost::json::object data;
	data["valid"] = valid;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_account_representative (boost::json::object const & request)
{
	// Validate required field: account
	if (!request.contains ("account"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: account");
	}

	auto const & account_value = request.at ("account");
	if (!account_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'account' must be a string");
	}

	std::string account_text = std::string (account_value.as_string ());
	nano::account account;

	if (account.decode_account (account_text))
	{
		return nano::rpc::v3::response_builder::error ("Invalid account format");
	}

	// Get account info
	auto transaction = node.store.tx_begin_read ();
	nano::account_info info;

	if (!node.store.account.get (transaction, account, info))
	{
		return nano::rpc::v3::response_builder::error ("Account not found");
	}

	boost::json::object data;
	data["representative"] = info.representative.to_account ();

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_accounts_balances (boost::json::object const & request)
{
	// Validate required field: accounts
	if (!request.contains ("accounts"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: accounts");
	}

	auto const & accounts_value = request.at ("accounts");
	if (!accounts_value.is_array ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'accounts' must be an array");
	}

	auto const & accounts_array = accounts_value.as_array ();
	boost::json::object balances_obj;

	bool include_only_confirmed = true;
	if (request.contains ("include_only_confirmed") && request.at ("include_only_confirmed").is_bool ())
	{
		include_only_confirmed = request.at ("include_only_confirmed").as_bool ();
	}

	for (auto const & account_val : accounts_array)
	{
		if (!account_val.is_string ())
		{
			continue;
		}

		std::string account_text = std::string (account_val.as_string ());
		nano::account account;

		if (!account.decode_account (account_text))
		{
			auto balance_pending = node.balance_pending (account, include_only_confirmed);

			boost::json::object account_data;
			account_data["balance"] = balance_pending.first.convert_to<std::string> ();
			account_data["pending"] = balance_pending.second.convert_to<std::string> ();
			account_data["receivable"] = balance_pending.second.convert_to<std::string> ();

			balances_obj[account_text] = account_data;
		}
	}

	boost::json::object data;
	data["balances"] = balances_obj;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_blocks_info (boost::json::object const & request)
{
	// Validate required field: hashes
	if (!request.contains ("hashes"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: hashes");
	}

	auto const & hashes_value = request.at ("hashes");
	if (!hashes_value.is_array ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'hashes' must be an array");
	}

	auto const & hashes_array = hashes_value.as_array ();
	boost::json::object blocks_obj;

	auto transaction = node.ledger.tx_begin_read ();

	for (auto const & hash_val : hashes_array)
	{
		if (!hash_val.is_string ())
		{
			continue;
		}

		std::string hash_text = std::string (hash_val.as_string ());
		nano::block_hash hash;

		if (!hash.decode_hex (hash_text))
		{
			auto block = node.ledger.any.block_get (transaction, hash);

			if (block)
			{
				auto account = node.ledger.any.block_account (transaction, hash);
				auto amount = node.ledger.any.block_amount (transaction, hash);
				auto balance = node.ledger.any.block_balance (transaction, hash);

				boost::json::object block_data;
				block_data["block_account"] = account.value_or (0).to_account ();
				block_data["contents"] = block->to_json ();

				if (amount)
				{
					block_data["amount"] = nano::amount{ amount->number () }.to_string_dec ();
				}
				if (balance)
				{
					block_data["balance"] = nano::amount{ balance->number () }.to_string_dec ();
				}

				auto height = node.ledger.any.block_height (transaction, hash);
				if (height > 0)
				{
					block_data["height"] = std::to_string (height);
					block_data["confirmed"] = true;
				}
				else
				{
					block_data["confirmed"] = false;
				}

				blocks_obj[hash_text] = block_data;
			}
		}
	}

	boost::json::object data;
	data["blocks"] = blocks_obj;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_block_create (boost::json::object const & request)
{
	// Validate required field: type
	if (!request.contains ("type"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: type");
	}

	auto const & type_value = request.at ("type");
	if (!type_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'type' must be a string");
	}

	std::string type = std::string (type_value.as_string ());

	// Validate required field: key (private key)
	if (!request.contains ("key"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: key (private key)");
	}

	auto const & key_value = request.at ("key");
	if (!key_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'key' must be a string");
	}

	std::string key_text = std::string (key_value.as_string ());
	nano::raw_key prv;
	if (prv.decode_hex (key_text))
	{
		return nano::rpc::v3::response_builder::error ("Invalid private key format");
	}

	nano::account pub (nano::pub_key (prv));

	// Parse optional work field
	uint64_t work = 0;
	if (request.contains ("work"))
	{
		auto const & work_value = request.at ("work");
		if (work_value.is_string ())
		{
			std::string work_text = std::string (work_value.as_string ());
			std::stringstream stream (work_text);
			try
			{
				stream >> std::hex >> work;
			}
			catch (...)
			{
				return nano::rpc::v3::response_builder::error ("Invalid work format");
			}
		}
	}

	// Check if work generation is available when work is not provided
	if (work == 0 && !node.work_generation_enabled ())
	{
		return nano::rpc::v3::response_builder::error ("Work generation is disabled and no work was provided");
	}

	// Parse common optional fields
	nano::account representative{};
	if (request.contains ("representative"))
	{
		auto const & rep_value = request.at ("representative");
		if (!rep_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'representative' must be a string");
		}
		std::string rep_text = std::string (rep_value.as_string ());
		if (representative.decode_account (rep_text))
		{
			return nano::rpc::v3::response_builder::error ("Invalid representative account format");
		}
	}

	nano::account destination{};
	if (request.contains ("destination"))
	{
		auto const & dest_value = request.at ("destination");
		if (!dest_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'destination' must be a string");
		}
		std::string dest_text = std::string (dest_value.as_string ());
		if (destination.decode_account (dest_text))
		{
			return nano::rpc::v3::response_builder::error ("Invalid destination account format");
		}
	}

	nano::block_hash source (0);
	if (request.contains ("source"))
	{
		auto const & source_value = request.at ("source");
		if (!source_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'source' must be a string");
		}
		std::string source_text = std::string (source_value.as_string ());
		if (source.decode_hex (source_text))
		{
			return nano::rpc::v3::response_builder::error ("Invalid source block hash");
		}
	}

	nano::block_hash previous (0);
	if (request.contains ("previous"))
	{
		auto const & prev_value = request.at ("previous");
		if (!prev_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'previous' must be a string");
		}
		std::string prev_text = std::string (prev_value.as_string ());
		if (previous.decode_hex (prev_text))
		{
			return nano::rpc::v3::response_builder::error ("Invalid previous block hash");
		}
	}

	nano::amount balance (0);
	if (request.contains ("balance"))
	{
		auto const & balance_value = request.at ("balance");
		if (!balance_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'balance' must be a string");
		}
		std::string balance_text = std::string (balance_value.as_string ());
		if (balance.decode_dec (balance_text))
		{
			return nano::rpc::v3::response_builder::error ("Invalid balance format");
		}
	}

	nano::amount amount (0);
	if (request.contains ("amount"))
	{
		auto const & amount_value = request.at ("amount");
		if (!amount_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'amount' must be a string");
		}
		std::string amount_text = std::string (amount_value.as_string ());
		if (amount.decode_dec (amount_text))
		{
			return nano::rpc::v3::response_builder::error ("Invalid amount format");
		}
	}

	nano::link link (0);
	if (request.contains ("link"))
	{
		auto const & link_value = request.at ("link");
		if (!link_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'link' must be a string");
		}
		std::string link_text = std::string (link_value.as_string ());
		if (link.decode_account (link_text))
		{
			if (link.decode_hex (link_text))
			{
				return nano::rpc::v3::response_builder::error ("Invalid link format");
			}
		}
	}
	else
	{
		// Derive link from source or destination
		if (!source.is_zero ())
		{
			link = source;
		}
		else if (!destination.is_zero ())
		{
			link = destination;
		}
	}

	// Fetch account info if previous/balance not provided
	if (previous.is_zero () && balance.number () == 0)
	{
		auto transaction = node.ledger.tx_begin_read ();
		previous = node.ledger.any.account_head (transaction, pub);
		balance = nano::amount{ node.ledger.any.account_balance (transaction, pub).value_or (0) };
	}

	// Build the block based on type
	nano::block_builder builder;
	std::shared_ptr<nano::block> block{ nullptr };
	nano::root root;
	std::error_code ec_build;

	if (type == "state")
	{
		if (!previous.is_zero () && !representative.is_zero () && (!link.is_zero () || request.contains ("link")))
		{
			block = builder.state ()
					.account (pub)
					.previous (previous)
					.representative (representative)
					.balance (balance)
					.link (link)
					.sign (prv, pub)
					.build (ec_build);
			root = previous.is_zero () ? nano::root (pub) : nano::root (previous);
		}
		else
		{
			return nano::rpc::v3::response_builder::error ("State block requires: previous, representative, link");
		}
	}
	else if (type == "open")
	{
		if (!representative.is_zero () && !source.is_zero ())
		{
			block = builder.open ()
					.account (pub)
					.source (source)
					.representative (representative)
					.sign (prv, pub)
					.build (ec_build);
			root = pub;
		}
		else
		{
			return nano::rpc::v3::response_builder::error ("Open block requires: source, representative");
		}
	}
	else if (type == "receive")
	{
		if (!source.is_zero () && !previous.is_zero ())
		{
			block = builder.receive ()
					.previous (previous)
					.source (source)
					.sign (prv, pub)
					.build (ec_build);
			root = previous;
		}
		else
		{
			return nano::rpc::v3::response_builder::error ("Receive block requires: source, previous");
		}
	}
	else if (type == "change")
	{
		if (!representative.is_zero () && !previous.is_zero ())
		{
			block = builder.change ()
					.previous (previous)
					.representative (representative)
					.sign (prv, pub)
					.build (ec_build);
			root = previous;
		}
		else
		{
			return nano::rpc::v3::response_builder::error ("Change block requires: previous, representative");
		}
	}
	else if (type == "send")
	{
		if (!destination.is_zero () && !previous.is_zero () && balance.number () != 0 && amount.number () != 0)
		{
			if (balance.number () >= amount.number ())
			{
				block = builder.send ()
						.previous (previous)
						.destination (destination)
						.balance (balance.number () - amount.number ())
						.sign (prv, pub)
						.build (ec_build);
				root = previous;
			}
			else
			{
				return nano::rpc::v3::response_builder::error ("Insufficient balance for send");
			}
		}
		else
		{
			return nano::rpc::v3::response_builder::error ("Send block requires: destination, previous, balance, amount");
		}
	}
	else
	{
		return nano::rpc::v3::response_builder::error ("Invalid block type: " + type);
	}

	if (!block || (ec_build && ec_build != nano::error_common::missing_work))
	{
		return nano::rpc::v3::response_builder::error ("Failed to build block");
	}

	// Set work if provided
	if (work != 0)
	{
		block->block_work_set (work);
	}

	// Build response
	boost::json::object data;
	data["hash"] = block->hash ().to_string ();
	data["difficulty"] = nano::to_string_hex (node.network_params.work.difficulty (*block));

	// Serialize block to JSON string
	std::string block_json;
	block->serialize_json (block_json);
	data["block"] = block_json;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_account_weight (boost::json::object const & request)
{
	// Validate required field: account
	if (!request.contains ("account"))
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	auto const & account_value = request.at ("account");
	if (!account_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	std::string account_text = std::string (account_value.as_string ());
	nano::account account;

	if (account.decode_account (account_text))
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	// Get voting weight for this account
	auto weight = node.weight (account);

	boost::json::object data;
	data["weight"] = weight.convert_to<std::string> ();

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_account_block_count (boost::json::object const & request)
{
	// Validate required field: account
	if (!request.contains ("account"))
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	auto const & account_value = request.at ("account");
	if (!account_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	std::string account_text = std::string (account_value.as_string ());
	nano::account account;

	if (account.decode_account (account_text))
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	// Get account info from ledger
	auto transaction = node.store.tx_begin_read ();
	nano::account_info info;

	if (!node.store.account.get (transaction, account, info))
	{
		return nano::rpc::v3::response_builder::error ("Account not found");
	}

	boost::json::object data;
	data["block_count"] = std::to_string (info.block_count);

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_block_account (boost::json::object const & request)
{
	// Validate required field: hash
	if (!request.contains ("hash"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: hash");
	}

	auto const & hash_value = request.at ("hash");
	if (!hash_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'hash' must be a string");
	}

	std::string hash_text = std::string (hash_value.as_string ());
	nano::block_hash hash;

	if (hash.decode_hex (hash_text))
	{
		return nano::rpc::v3::response_builder::error ("Invalid block hash");
	}

	// Get block from ledger
	auto transaction = node.ledger.tx_begin_read ();
	auto block = node.ledger.any.block_get (transaction, hash);

	if (!block)
	{
		return nano::rpc::v3::response_builder::error ("Block not found");
	}

	boost::json::object data;
	data["account"] = block->account ().to_account ();

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_nano_to_raw (boost::json::object const & request)
{
	// Validate required field: amount
	if (!request.contains ("amount"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: amount");
	}

	auto const & amount_value = request.at ("amount");
	if (!amount_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'amount' must be a string");
	}

	std::string amount_text = std::string (amount_value.as_string ());
	nano::amount amount;

	if (amount.decode_dec (amount_text))
	{
		return nano::rpc::v3::response_builder::error ("Invalid amount");
	}

	// Convert Nano to raw (multiply by 10^30)
	auto result = amount.number () * nano::nano_ratio;

	// Check for overflow
	if (result <= amount.number ())
	{
		return nano::rpc::v3::response_builder::error ("Invalid amount (overflow)");
	}

	boost::json::object data;
	data["amount"] = result.convert_to<std::string> ();

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_raw_to_nano (boost::json::object const & request)
{
	// Validate required field: amount
	if (!request.contains ("amount"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: amount");
	}

	auto const & amount_value = request.at ("amount");
	if (!amount_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'amount' must be a string");
	}

	std::string amount_text = std::string (amount_value.as_string ());
	nano::amount amount;

	if (amount.decode_dec (amount_text))
	{
		return nano::rpc::v3::response_builder::error ("Invalid amount");
	}

	// Convert raw to Nano (divide by 10^30)
	auto result = amount.number () / nano::nano_ratio;

	boost::json::object data;
	data["amount"] = result.convert_to<std::string> ();

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_key_create (boost::json::object const & request)
{
	// Generate a new keypair
	nano::keypair pair;

	boost::json::object data;
	data["private"] = pair.prv.to_string ();
	data["public"] = pair.pub.to_string ();
	data["account"] = pair.pub.to_account ();

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_key_expand (boost::json::object const & request)
{
	// Validate required field: key
	if (!request.contains ("key"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: key");
	}

	auto const & key_value = request.at ("key");
	if (!key_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'key' must be a string");
	}

	std::string key_text = std::string (key_value.as_string ());
	nano::raw_key prv;

	if (prv.decode_hex (key_text))
	{
		return nano::rpc::v3::response_builder::error ("Bad private key");
	}

	nano::public_key pub (nano::pub_key (prv));

	boost::json::object data;
	data["private"] = prv.to_string ();
	data["public"] = pub.to_string ();
	data["account"] = pub.to_account ();

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_receivable (boost::json::object const & request)
{
	// Validate required field: account
	if (!request.contains ("account"))
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	auto const & account_value = request.at ("account");
	if (!account_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	std::string account_text = std::string (account_value.as_string ());
	nano::account account;

	if (account.decode_account (account_text))
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	// Parse optional parameters
	uint64_t count = std::numeric_limits<uint64_t>::max ();
	if (request.contains ("count"))
	{
		auto const & count_value = request.at ("count");
		if (count_value.is_string ())
		{
			std::string count_text = std::string (count_value.as_string ());
			std::stringstream ss (count_text);
			ss >> count;
			if (ss.fail () || count == 0)
			{
				return nano::rpc::v3::response_builder::error ("Invalid count");
			}
		}
		else if (count_value.is_int64 ())
		{
			count = count_value.as_int64 ();
		}
	}

	uint64_t offset = 0;
	if (request.contains ("offset"))
	{
		auto const & offset_value = request.at ("offset");
		if (offset_value.is_string ())
		{
			std::string offset_text = std::string (offset_value.as_string ());
			std::stringstream ss (offset_text);
			ss >> offset;
			if (ss.fail ())
			{
				return nano::rpc::v3::response_builder::error ("Invalid offset");
			}
		}
		else if (offset_value.is_int64 ())
		{
			offset = offset_value.as_int64 ();
		}
	}

	nano::amount threshold (0);
	if (request.contains ("threshold"))
	{
		auto const & threshold_value = request.at ("threshold");
		if (threshold_value.is_string ())
		{
			std::string threshold_text = std::string (threshold_value.as_string ());
			if (threshold.decode_dec (threshold_text))
			{
				return nano::rpc::v3::response_builder::error ("Bad threshold");
			}
		}
	}

	bool const source = request.contains ("source") && request.at ("source").is_bool () ? request.at ("source").as_bool () : false;
	bool const min_version = request.contains ("min_version") && request.at ("min_version").is_bool () ? request.at ("min_version").as_bool () : false;
	bool const include_active = request.contains ("include_active") && request.at ("include_active").is_bool () ? request.at ("include_active").as_bool () : false;
	bool const include_only_confirmed = request.contains ("include_only_confirmed") && request.at ("include_only_confirmed").is_bool () ? request.at ("include_only_confirmed").as_bool () : true;
	bool const sorting = request.contains ("sorting") && request.at ("sorting").is_bool () ? request.at ("sorting").as_bool () : false;

	auto simple = threshold.is_zero () && !source && !min_version && !sorting;
	bool const should_sort = sorting && !simple;

	auto transaction = node.ledger.tx_begin_read ();
	auto offset_counter = offset;

	// Helper lambda for block confirmation check
	auto block_confirmed = [&] (nano::block_hash const & hash) -> bool {
		if (include_active && !include_only_confirmed)
		{
			return true;
		}
		else if (node.ledger.confirmed.block_exists_or_pruned (transaction, hash))
		{
			return true;
		}
		else if (!include_only_confirmed)
		{
			auto block = node.ledger.any.block_get (transaction, hash);
			return (block != nullptr && !node.active.active (*block));
		}
		return false;
	};

	// Helper lambda for epoch to string conversion
	auto epoch_as_string = [] (nano::epoch epoch) -> char const * {
		switch (epoch)
		{
			case nano::epoch::epoch_2:
				return "2";
			case nano::epoch::epoch_1:
				return "1";
			default:
				return "0";
		}
	};

	// Collect receivable blocks
	boost::json::object blocks_obj;
	std::vector<std::pair<std::string, boost::json::object>> hash_obj_pairs;
	std::vector<std::pair<std::string, nano::uint128_t>> hash_amount_pairs;

	for (auto i = node.store.pending.begin (transaction, nano::pending_key (account, 0)),
			  n = node.store.pending.end (transaction);
		 i != n && nano::pending_key (i->first).account == account && (should_sort || blocks_obj.size () < count);
		 ++i)
	{
		nano::pending_key const & key (i->first);
		if (block_confirmed (key.hash))
		{
			if (!should_sort && offset_counter > 0)
			{
				--offset_counter;
				continue;
			}

			if (simple)
			{
				// Simple format: just hash as string
				blocks_obj[key.hash.to_string ()] = "";
			}
			else
			{
				nano::pending_info const & info (i->second);
				if (info.amount.number () >= threshold.number ())
				{
					if (source || min_version)
					{
						boost::json::object pending_obj;
						pending_obj["amount"] = info.amount.number ().convert_to<std::string> ();
						if (source)
						{
							pending_obj["source"] = info.source.to_account ();
						}
						if (min_version)
						{
							pending_obj["min_version"] = epoch_as_string (info.epoch);
						}

						if (should_sort)
						{
							hash_obj_pairs.emplace_back (key.hash.to_string (), pending_obj);
						}
						else
						{
							blocks_obj[key.hash.to_string ()] = pending_obj;
						}
					}
					else
					{
						if (should_sort)
						{
							hash_amount_pairs.emplace_back (key.hash.to_string (), info.amount.number ());
						}
						else
						{
							blocks_obj[key.hash.to_string ()] = info.amount.number ().convert_to<std::string> ();
						}
					}
				}
			}
		}
	}

	// Sort if requested
	if (should_sort)
	{
		if (source || min_version)
		{
			std::stable_sort (hash_obj_pairs.begin (), hash_obj_pairs.end (),
			[] (auto const & lhs, auto const & rhs) {
				auto lhs_amount_str = lhs.second.at ("amount").as_string ();
				auto rhs_amount_str = rhs.second.at ("amount").as_string ();
				nano::amount lhs_amount, rhs_amount;
				lhs_amount.decode_dec (std::string (lhs_amount_str));
				rhs_amount.decode_dec (std::string (rhs_amount_str));
				return lhs_amount.number () > rhs_amount.number ();
			});
			for (auto i = offset, j = offset + count; i < hash_obj_pairs.size () && i < j; ++i)
			{
				blocks_obj[hash_obj_pairs[i].first] = hash_obj_pairs[i].second;
			}
		}
		else
		{
			std::stable_sort (hash_amount_pairs.begin (), hash_amount_pairs.end (),
			[] (auto const & lhs, auto const & rhs) {
				return lhs.second > rhs.second;
			});
			for (auto i = offset, j = offset + count; i < hash_amount_pairs.size () && i < j; ++i)
			{
				blocks_obj[hash_amount_pairs[i].first] = hash_amount_pairs[i].second.convert_to<std::string> ();
			}
		}
	}

	boost::json::object data;
	data["blocks"] = blocks_obj;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_receivable_exists (boost::json::object const & request)
{
	// Validate required field: hash
	if (!request.contains ("hash"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: hash");
	}

	auto const & hash_value = request.at ("hash");
	if (!hash_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'hash' must be a string");
	}

	std::string hash_text = std::string (hash_value.as_string ());
	nano::block_hash hash;

	if (hash.decode_hex (hash_text))
	{
		return nano::rpc::v3::response_builder::error ("Invalid block hash");
	}

	bool const include_active = request.contains ("include_active") && request.at ("include_active").is_bool () ? request.at ("include_active").as_bool () : false;
	bool const include_only_confirmed = request.contains ("include_only_confirmed") && request.at ("include_only_confirmed").is_bool () ? request.at ("include_only_confirmed").as_bool () : true;

	auto transaction = node.ledger.tx_begin_read ();
	auto block = node.ledger.any.block_get (transaction, hash);

	if (!block)
	{
		return nano::rpc::v3::response_builder::error ("Block not found");
	}

	bool exists = false;
	if (block->is_send ())
	{
		exists = node.ledger.any.pending_get (transaction, nano::pending_key{ block->destination (), hash }).has_value ();
	}

	// Check confirmation status
	auto block_confirmed = [&] () -> bool {
		if (include_active && !include_only_confirmed)
		{
			return true;
		}
		else if (node.ledger.confirmed.block_exists_or_pruned (transaction, hash))
		{
			return true;
		}
		else if (!include_only_confirmed)
		{
			return !node.active.active (*block);
		}
		return false;
	};

	exists = exists && block_confirmed ();

	boost::json::object data;
	data["exists"] = exists ? "1" : "0";

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_representatives (boost::json::object const & request)
{
	// Parse optional count parameter
	uint64_t count = std::numeric_limits<uint64_t>::max ();
	if (request.contains ("count"))
	{
		auto const & count_value = request.at ("count");
		if (count_value.is_string ())
		{
			std::string count_text = std::string (count_value.as_string ());
			std::stringstream ss (count_text);
			ss >> count;
			if (ss.fail () || count == 0)
			{
				return nano::rpc::v3::response_builder::error ("Invalid count");
			}
		}
		else if (count_value.is_int64 ())
		{
			count = count_value.as_int64 ();
		}
	}

	bool const sorting = request.contains ("sorting") && request.at ("sorting").is_bool () ? request.at ("sorting").as_bool () : false;

	boost::json::object representatives_obj;
	auto rep_amounts = node.ledger.rep_weights.get_rep_amounts ();

	if (!sorting)
	{
		// Simple: iterate through representatives
		for (auto const & rep_amount : rep_amounts)
		{
			auto const & account = rep_amount.first;
			auto const & amount = rep_amount.second;
			representatives_obj[account.to_account ()] = amount.convert_to<std::string> ();

			if (representatives_obj.size () >= count)
			{
				break;
			}
		}
	}
	else
	{
		// Sorting: sort by weight descending
		std::vector<std::pair<nano::uint128_t, std::string>> representation;
		for (auto const & rep_amount : rep_amounts)
		{
			auto const & account = rep_amount.first;
			auto const & amount = rep_amount.second;
			representation.emplace_back (amount, account.to_account ());
		}

		std::sort (representation.begin (), representation.end ());
		std::reverse (representation.begin (), representation.end ());

		for (auto i = representation.begin (), n = representation.end (); i != n && representatives_obj.size () < count; ++i)
		{
			representatives_obj[i->second] = i->first.convert_to<std::string> ();
		}
	}

	boost::json::object data;
	data["representatives"] = representatives_obj;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_representatives_online (boost::json::object const & request)
{
	// Parse optional accounts parameter for filtering
	std::vector<nano::account> accounts_to_filter;
	if (request.contains ("accounts"))
	{
		auto const & accounts_value = request.at ("accounts");
		if (!accounts_value.is_array ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'accounts' must be an array");
		}

		auto const & accounts_array = accounts_value.as_array ();
		for (auto const & account_val : accounts_array)
		{
			if (!account_val.is_string ())
			{
				return nano::rpc::v3::response_builder::error ("Account must be a string");
			}

			std::string account_text = std::string (account_val.as_string ());
			nano::account account;

			if (account.decode_account (account_text))
			{
				return nano::rpc::v3::response_builder::error ("Invalid account format: " + account_text);
			}

			accounts_to_filter.push_back (account);
		}
	}

	// Parse optional weight parameter
	bool const weight = request.contains ("weight") && request.at ("weight").is_bool () ? request.at ("weight").as_bool () : false;

	// Get online representatives
	auto reps = node.online_reps.list ();

	// Build response
	boost::json::object representatives_obj;

	for (auto const & rep : reps)
	{
		// If filtering by accounts, check if this rep is in the filter list
		if (!accounts_to_filter.empty ())
		{
			auto found = std::find (accounts_to_filter.begin (), accounts_to_filter.end (), rep);
			if (found == accounts_to_filter.end ())
			{
				// Not in filter list, skip this rep
				continue;
			}
			// Remove from filter list (for efficiency, though not strictly necessary)
			accounts_to_filter.erase (found);

			// If filter list is now empty, we can break early
			if (accounts_to_filter.empty () && request.contains ("accounts"))
			{
				// We've found all requested accounts
				break;
			}
		}

		// Add to response
		if (weight)
		{
			// Include weight information
			auto account_weight = node.ledger.weight (rep);
			boost::json::object weight_obj;
			weight_obj["weight"] = account_weight.convert_to<std::string> ();
			representatives_obj[rep.to_account ()] = weight_obj;
		}
		else
		{
			// Just the account string
			representatives_obj[rep.to_account ()] = "";
		}
	}

	boost::json::object data;
	data["representatives"] = representatives_obj;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_online_reps (boost::json::object const & request)
{
	// Get online representative weight metrics
	auto online_weight = node.online_reps.online ();
	auto trended_weight = node.online_reps.trended ();
	auto delta_weight = node.online_reps.delta ();
	auto online_reps_list = node.online_reps.list ();

	// Build response with weight metrics
	boost::json::object data;
	data["online_stake_total"] = online_weight.convert_to<std::string> ();
	data["trended_stake_total"] = trended_weight.convert_to<std::string> ();
	data["quorum_delta"] = delta_weight.convert_to<std::string> ();
	data["online_reps_count"] = online_reps_list.size ();

	// Optionally include the list of representatives if requested
	bool include_list = request.contains ("include_list") && request.at ("include_list").is_bool () ? request.at ("include_list").as_bool () : false;

	if (include_list)
	{
		boost::json::array reps_array;
		for (auto const & rep : online_reps_list)
		{
			reps_array.push_back (boost::json::value (rep.to_account ()));
		}
		data["representatives"] = reps_array;
	}

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_account_get (boost::json::object const & request)
{
	// Validate required field: key
	if (!request.contains ("key"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: key");
	}

	auto const & key_value = request.at ("key");
	if (!key_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'key' must be a string");
	}

	std::string key_text = std::string (key_value.as_string ());
	nano::public_key pub;

	if (pub.decode_hex (key_text))
	{
		return nano::rpc::v3::response_builder::error ("Bad public key");
	}

	boost::json::object data;
	data["account"] = pub.to_account ();

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_account_key (boost::json::object const & request)
{
	// Validate required field: account
	if (!request.contains ("account"))
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	auto const & account_value = request.at ("account");
	if (!account_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	std::string account_text = std::string (account_value.as_string ());
	nano::account account;

	if (account.decode_account (account_text))
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	boost::json::object data;
	data["key"] = account.to_string ();

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_account_count (boost::json::object const & request)
{
	auto size = node.ledger.account_count ();

	boost::json::object data;
	data["count"] = std::to_string (size);

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_frontiers (boost::json::object const & request)
{
	// Validate required field: account (starting account)
	if (!request.contains ("account"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: account");
	}

	auto const & account_value = request.at ("account");
	if (!account_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'account' must be a string");
	}

	std::string account_text = std::string (account_value.as_string ());
	nano::account start_account;

	if (start_account.decode_account (account_text))
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	// Parse optional count parameter
	uint64_t count = std::numeric_limits<uint64_t>::max ();
	if (request.contains ("count"))
	{
		auto const & count_value = request.at ("count");
		if (count_value.is_string ())
		{
			std::string count_text = std::string (count_value.as_string ());
			std::stringstream ss (count_text);
			ss >> count;
			if (ss.fail () || count == 0)
			{
				return nano::rpc::v3::response_builder::error ("Invalid count");
			}
		}
		else if (count_value.is_int64 ())
		{
			count = count_value.as_int64 ();
		}
	}

	// Get frontiers from ledger
	auto transaction = node.ledger.tx_begin_read ();
	boost::json::object frontiers_obj;

	for (auto i = node.store.account.begin (transaction, start_account), n = node.store.account.end (transaction); i != n && frontiers_obj.size () < count; ++i)
	{
		frontiers_obj[i->first.to_account ()] = i->second.head.to_string ();
	}

	boost::json::object data;
	data["frontiers"] = frontiers_obj;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_accounts_frontiers (boost::json::object const & request)
{
	// Validate required field: accounts
	if (!request.contains ("accounts"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: accounts");
	}

	auto const & accounts_value = request.at ("accounts");
	if (!accounts_value.is_array ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'accounts' must be an array");
	}

	auto const & accounts_array = accounts_value.as_array ();
	boost::json::object frontiers_obj;

	auto transaction = node.ledger.tx_begin_read ();

	for (auto const & account_val : accounts_array)
	{
		if (!account_val.is_string ())
		{
			continue;
		}

		std::string account_text = std::string (account_val.as_string ());
		nano::account account;

		if (!account.decode_account (account_text))
		{
			auto head = node.ledger.any.account_head (transaction, account);
			if (!head.is_zero ())
			{
				frontiers_obj[account_text] = head.to_string ();
			}
		}
	}

	boost::json::object data;
	data["frontiers"] = frontiers_obj;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_ledger (boost::json::object const & request)
{
	// Parse optional count parameter
	uint64_t count = std::numeric_limits<uint64_t>::max ();
	if (request.contains ("count"))
	{
		auto const & count_value = request.at ("count");
		if (count_value.is_string ())
		{
			std::string count_text = std::string (count_value.as_string ());
			std::stringstream ss (count_text);
			ss >> count;
			if (ss.fail () || count == 0)
			{
				return nano::rpc::v3::response_builder::error ("Invalid count");
			}
		}
		else if (count_value.is_int64 ())
		{
			count = count_value.as_int64 ();
		}
	}

	// Parse optional threshold parameter
	nano::amount threshold (0);
	if (request.contains ("threshold"))
	{
		auto const & threshold_value = request.at ("threshold");
		if (threshold_value.is_string ())
		{
			std::string threshold_text = std::string (threshold_value.as_string ());
			if (threshold.decode_dec (threshold_text))
			{
				return nano::rpc::v3::response_builder::error ("Bad threshold");
			}
		}
	}

	// Parse optional account parameter (starting account)
	nano::account start_account{};
	if (request.contains ("account"))
	{
		auto const & account_value = request.at ("account");
		if (!account_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'account' must be a string");
		}
		std::string account_text = std::string (account_value.as_string ());
		if (start_account.decode_account (account_text))
		{
			return nano::rpc::v3::response_builder::error ("Bad account number");
		}
	}

	// Parse optional modified_since parameter
	uint64_t modified_since = 0;
	if (request.contains ("modified_since"))
	{
		auto const & modified_value = request.at ("modified_since");
		if (modified_value.is_string ())
		{
			std::string modified_text = std::string (modified_value.as_string ());
			std::stringstream ss (modified_text);
			ss >> modified_since;
			if (ss.fail ())
			{
				return nano::rpc::v3::response_builder::error ("Invalid timestamp");
			}
		}
		else if (modified_value.is_int64 ())
		{
			modified_since = modified_value.as_int64 ();
		}
	}

	// Parse optional flags
	bool const sorting = request.contains ("sorting") && request.at ("sorting").is_bool () ? request.at ("sorting").as_bool () : false;
	bool const representative = request.contains ("representative") && request.at ("representative").is_bool () ? request.at ("representative").as_bool () : false;
	bool const weight = request.contains ("weight") && request.at ("weight").is_bool () ? request.at ("weight").as_bool () : false;
	bool const pending = request.contains ("pending") && request.at ("pending").is_bool () ? request.at ("pending").as_bool () : false;
	bool const receivable = request.contains ("receivable") && request.at ("receivable").is_bool () ? request.at ("receivable").as_bool () : pending;

	auto transaction = node.ledger.tx_begin_read ();
	boost::json::object accounts_obj;

	if (!sorting)
	{
		// Simple iteration
		for (auto i = node.store.account.begin (transaction, start_account), n = node.store.account.end (transaction); i != n && accounts_obj.size () < count; ++i)
		{
			nano::account_info const & info = i->second;
			if (info.modified >= modified_since && (receivable || info.balance.number () >= threshold.number ()))
			{
				nano::account const & account = i->first;
				boost::json::object account_data;

				if (receivable)
				{
					auto account_receivable = node.ledger.account_receivable (transaction, account);
					if (info.balance.number () + account_receivable < threshold.number ())
					{
						continue;
					}
					account_data["pending"] = account_receivable.convert_to<std::string> ();
					account_data["receivable"] = account_receivable.convert_to<std::string> ();
				}

				account_data["frontier"] = info.head.to_string ();
				account_data["open_block"] = info.open_block.to_string ();
				account_data["representative_block"] = node.ledger.representative_block (transaction, info.head).to_string ();
				account_data["balance"] = nano::amount{ info.balance.number () }.to_string_dec ();
				account_data["modified_timestamp"] = std::to_string (info.modified);
				account_data["block_count"] = std::to_string (info.block_count);

				if (representative)
				{
					account_data["representative"] = info.representative.to_account ();
				}

				if (weight)
				{
					auto account_weight = node.ledger.weight_exact (transaction, account);
					account_data["weight"] = account_weight.convert_to<std::string> ();
				}

				accounts_obj[account.to_account ()] = account_data;
			}
		}
	}
	else
	{
		// Sorting by balance
		std::vector<std::pair<nano::uint128_t, nano::account>> ledger_list;
		for (auto i = node.store.account.begin (transaction, start_account), n = node.store.account.end (transaction); i != n; ++i)
		{
			nano::account_info const & info = i->second;
			if (info.modified >= modified_since)
			{
				ledger_list.emplace_back (info.balance.number (), i->first);
			}
		}

		std::sort (ledger_list.begin (), ledger_list.end ());
		std::reverse (ledger_list.begin (), ledger_list.end ());

		for (auto const & [balance, account] : ledger_list)
		{
			if (accounts_obj.size () >= count)
			{
				break;
			}

			if (balance >= threshold.number ())
			{
				nano::account_info info;
				if (!node.store.account.get (transaction, account, info))
				{
					boost::json::object account_data;

					if (receivable)
					{
						auto account_receivable = node.ledger.account_receivable (transaction, account);
						account_data["pending"] = account_receivable.convert_to<std::string> ();
						account_data["receivable"] = account_receivable.convert_to<std::string> ();
					}

					account_data["frontier"] = info.head.to_string ();
					account_data["open_block"] = info.open_block.to_string ();
					account_data["representative_block"] = node.ledger.representative_block (transaction, info.head).to_string ();
					account_data["balance"] = balance.convert_to<std::string> ();
					account_data["modified_timestamp"] = std::to_string (info.modified);
					account_data["block_count"] = std::to_string (info.block_count);

					if (representative)
					{
						account_data["representative"] = info.representative.to_account ();
					}

					if (weight)
					{
						auto account_weight = node.ledger.weight_exact (transaction, account);
						account_data["weight"] = account_weight.convert_to<std::string> ();
					}

					accounts_obj[account.to_account ()] = account_data;
				}
			}
		}
	}

	boost::json::object data;
	data["accounts"] = accounts_obj;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_account_history (boost::json::object const & request)
{
	// Parse required account or optional head parameter
	nano::account account{};
	nano::block_hash hash{};
	bool has_head = false;

	if (request.contains ("head"))
	{
		auto const & head_value = request.at ("head");
		if (!head_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'head' must be a string");
		}
		std::string head_text = std::string (head_value.as_string ());
		if (hash.decode_hex (head_text))
		{
			return nano::rpc::v3::response_builder::error ("Bad hash number");
		}
		has_head = true;
	}

	if (!has_head)
	{
		if (!request.contains ("account"))
		{
			return nano::rpc::v3::response_builder::error ("Missing required field: account or head");
		}

		auto const & account_value = request.at ("account");
		if (!account_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'account' must be a string");
		}

		std::string account_text = std::string (account_value.as_string ());
		if (account.decode_account (account_text))
		{
			return nano::rpc::v3::response_builder::error ("Bad account number");
		}
	}

	// Parse optional parameters
	uint64_t count = std::numeric_limits<uint64_t>::max ();
	if (request.contains ("count"))
	{
		auto const & count_value = request.at ("count");
		if (count_value.is_string ())
		{
			std::string count_text = std::string (count_value.as_string ());
			std::stringstream ss (count_text);
			ss >> count;
			if (ss.fail () || count == 0)
			{
				return nano::rpc::v3::response_builder::error ("Invalid count");
			}
		}
		else if (count_value.is_int64 ())
		{
			count = count_value.as_int64 ();
		}
	}

	uint64_t offset = 0;
	if (request.contains ("offset"))
	{
		auto const & offset_value = request.at ("offset");
		if (offset_value.is_string ())
		{
			std::string offset_text = std::string (offset_value.as_string ());
			std::stringstream ss (offset_text);
			ss >> offset;
			if (ss.fail ())
			{
				return nano::rpc::v3::response_builder::error ("Invalid offset");
			}
		}
		else if (offset_value.is_int64 ())
		{
			offset = offset_value.as_int64 ();
		}
	}

	bool const reverse = request.contains ("reverse") && request.at ("reverse").is_bool () ? request.at ("reverse").as_bool () : false;
	bool const raw = request.contains ("raw") && request.at ("raw").is_bool () ? request.at ("raw").as_bool () : false;

	auto transaction = node.ledger.tx_begin_read ();

	// Determine starting hash
	if (has_head)
	{
		if (!node.ledger.any.block_exists (transaction, hash))
		{
			return nano::rpc::v3::response_builder::error ("Block not found");
		}
		auto block_account = node.ledger.any.block_account (transaction, hash);
		if (block_account)
		{
			account = block_account.value ();
		}
		else
		{
			return nano::rpc::v3::response_builder::error ("Block not found");
		}
	}
	else
	{
		if (reverse)
		{
			nano::account_info info;
			if (node.store.account.get (transaction, account, info))
			{
				return nano::rpc::v3::response_builder::error ("Account not found");
			}
			hash = info.open_block;
		}
		else
		{
			hash = node.ledger.any.account_head (transaction, account);
		}
	}

	// Build history
	boost::json::array history_array;
	auto block = node.ledger.any.block_get (transaction, hash);

	while (block != nullptr && history_array.size () < count)
	{
		if (offset > 0)
		{
			--offset;
		}
		else
		{
			boost::json::object entry;

			// Determine block type and extract info
			auto block_type = block->type ();
			auto amount = node.ledger.any.block_amount (transaction, hash);
			auto balance = node.ledger.any.block_balance (transaction, hash);

			if (block_type == nano::block_type::state)
			{
				auto state_block = dynamic_cast<nano::state_block const *> (block.get ());
				if (state_block)
				{
					auto previous_balance = node.ledger.any.block_balance (transaction, state_block->hashables.previous).value_or (0);

					if (balance && balance.value () < previous_balance)
					{
						// Send
						entry["type"] = "send";
						entry["account"] = state_block->hashables.link.to_account ();
						entry["amount"] = (previous_balance.number () - balance.value ().number ()).convert_to<std::string> ();
					}
					else if (state_block->hashables.link.is_zero ())
					{
						// Change
						if (raw)
						{
							entry["type"] = "change";
							entry["representative"] = state_block->hashables.representative.to_account ();
						}
					}
					else
					{
						// Receive
						entry["type"] = "receive";
						if (amount)
						{
							auto source_account = node.ledger.any.block_account (transaction, state_block->hashables.link.as_block_hash ());
							if (source_account)
							{
								entry["account"] = source_account.value ().to_account ();
							}
							entry["amount"] = amount.value ().number ().convert_to<std::string> ();
						}
					}

					if (raw)
					{
						entry["representative"] = state_block->hashables.representative.to_account ();
						entry["link"] = state_block->hashables.link.to_string ();
						entry["balance"] = state_block->hashables.balance.to_string_dec ();
						entry["previous"] = state_block->hashables.previous.to_string ();
					}
				}
			}
			else if (block_type == nano::block_type::send)
			{
				auto send_block = dynamic_cast<nano::send_block const *> (block.get ());
				if (send_block)
				{
					entry["type"] = "send";
					entry["account"] = send_block->hashables.destination.to_account ();
					if (amount)
					{
						entry["amount"] = amount.value ().number ().convert_to<std::string> ();
					}
					if (raw)
					{
						entry["destination"] = send_block->hashables.destination.to_account ();
						entry["balance"] = send_block->hashables.balance.to_string_dec ();
						entry["previous"] = send_block->hashables.previous.to_string ();
					}
				}
			}
			else if (block_type == nano::block_type::receive || block_type == nano::block_type::open)
			{
				entry["type"] = "receive";
				if (amount)
				{
					entry["amount"] = amount.value ().number ().convert_to<std::string> ();
				}
			}
			else if (block_type == nano::block_type::change)
			{
				if (raw)
				{
					auto change_block = dynamic_cast<nano::change_block const *> (block.get ());
					if (change_block)
					{
						entry["type"] = "change";
						entry["representative"] = change_block->hashables.representative.to_account ();
						entry["previous"] = change_block->hashables.previous.to_string ();
					}
				}
			}

			if (!entry.empty ())
			{
				entry["local_timestamp"] = std::to_string (block->sideband ().timestamp);
				entry["height"] = std::to_string (block->sideband ().height);
				entry["hash"] = hash.to_string ();
				entry["confirmed"] = node.ledger.confirmed.block_exists_or_pruned (transaction, hash);

				if (raw)
				{
					entry["work"] = nano::to_string_hex (block->block_work ());
					entry["signature"] = block->block_signature ().to_string ();
				}

				history_array.push_back (entry);
			}
		}

		// Move to next/previous block
		hash = reverse ? node.ledger.any.block_successor (transaction, hash).value_or (0) : block->previous ();
		block = node.ledger.any.block_get (transaction, hash);
	}

	boost::json::object data;
	data["account"] = account.to_account ();
	data["history"] = history_array;

	if (!hash.is_zero ())
	{
		data[reverse ? "next" : "previous"] = hash.to_string ();
	}

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_block_hash (boost::json::object const & request)
{
	// Validate required field: block
	if (!request.contains ("block"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: block");
	}

	auto const & block_value = request.at ("block");
	if (!block_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'block' must be a string");
	}

	std::string block_text = std::string (block_value.as_string ());

	// Parse the block JSON string into property_tree
	boost::property_tree::ptree block_tree;
	std::shared_ptr<nano::block> block;
	try
	{
		std::stringstream block_stream (block_text);
		boost::property_tree::read_json (block_stream, block_tree);
		block = nano::deserialize_block_json (block_tree);
	}
	catch (std::exception const &)
	{
		return nano::rpc::v3::response_builder::error ("Invalid block");
	}

	if (!block)
	{
		return nano::rpc::v3::response_builder::error ("Invalid block");
	}

	boost::json::object data;
	data["hash"] = block->hash ().to_string ();

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_blocks (boost::json::object const & request)
{
	// Validate required field: hashes
	if (!request.contains ("hashes"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: hashes");
	}

	auto const & hashes_value = request.at ("hashes");
	if (!hashes_value.is_array ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'hashes' must be an array");
	}

	bool const json_block = request.contains ("json_block") && request.at ("json_block").is_bool () ? request.at ("json_block").as_bool () : false;

	auto const & hashes_array = hashes_value.as_array ();
	boost::json::object blocks_obj;

	auto transaction = node.ledger.tx_begin_read ();

	for (auto const & hash_val : hashes_array)
	{
		if (!hash_val.is_string ())
		{
			continue;
		}

		std::string hash_text = std::string (hash_val.as_string ());
		nano::block_hash hash;

		if (!hash.decode_hex (hash_text))
		{
			auto block = node.ledger.any.block_get (transaction, hash);

			if (block)
			{
				if (json_block)
				{
					blocks_obj[hash_text] = block->to_json ();
				}
				else
				{
					std::string contents;
					block->serialize_json (contents);
					blocks_obj[hash_text] = contents;
				}
			}
			else
			{
				return nano::rpc::v3::response_builder::error ("Block not found");
			}
		}
		else
		{
			return nano::rpc::v3::response_builder::error ("Bad hash number");
		}
	}

	boost::json::object data;
	data["blocks"] = blocks_obj;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_chain (boost::json::object const & request)
{
	// Validate required field: block
	if (!request.contains ("block"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: block");
	}

	auto const & block_value = request.at ("block");
	if (!block_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'block' must be a string");
	}

	std::string hash_text = std::string (block_value.as_string ());
	nano::block_hash hash;

	if (hash.decode_hex (hash_text))
	{
		return nano::rpc::v3::response_builder::error ("Invalid block hash");
	}

	// Parse count parameter
	uint64_t count = std::numeric_limits<uint64_t>::max ();
	if (request.contains ("count"))
	{
		auto const & count_value = request.at ("count");
		if (count_value.is_string ())
		{
			std::string count_text = std::string (count_value.as_string ());
			std::stringstream ss (count_text);
			ss >> count;
			if (ss.fail () || count == 0)
			{
				return nano::rpc::v3::response_builder::error ("Invalid count");
			}
		}
		else if (count_value.is_int64 ())
		{
			count = count_value.as_int64 ();
		}
	}

	// Parse offset parameter
	uint64_t offset = 0;
	if (request.contains ("offset"))
	{
		auto const & offset_value = request.at ("offset");
		if (offset_value.is_string ())
		{
			std::string offset_text = std::string (offset_value.as_string ());
			std::stringstream ss (offset_text);
			ss >> offset;
			if (ss.fail ())
			{
				return nano::rpc::v3::response_builder::error ("Invalid offset");
			}
		}
		else if (offset_value.is_int64 ())
		{
			offset = offset_value.as_int64 ();
		}
	}

	// Parse reverse parameter
	bool const reverse = request.contains ("reverse") && request.at ("reverse").is_bool () ? request.at ("reverse").as_bool () : false;
	bool successors = !reverse;

	boost::json::array blocks_array;
	auto transaction = node.ledger.tx_begin_read ();

	while (!hash.is_zero () && blocks_array.size () < count)
	{
		auto block = node.ledger.any.block_get (transaction, hash);
		if (block)
		{
			if (offset > 0)
			{
				--offset;
			}
			else
			{
				blocks_array.push_back (boost::json::value (hash.to_string ()));
			}
			hash = successors ? node.ledger.any.block_successor (transaction, hash).value_or (0) : block->previous ();
		}
		else
		{
			hash.clear ();
		}
	}

	boost::json::object data;
	data["blocks"] = blocks_array;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_node_id (boost::json::object const & request)
{
	boost::json::object data;
	data["public"] = node.node_id.pub.to_string ();
	data["as_account"] = node.node_id.pub.to_account ();
	data["node_id"] = node.node_id.pub.to_node_id ();

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_active_difficulty (boost::json::object const & request)
{
	bool const include_trend = request.contains ("include_trend") && request.at ("include_trend").is_bool () ? request.at ("include_trend").as_bool () : false;

	auto const multiplier_active = 1.0;
	auto const default_difficulty = node.default_difficulty (nano::work_version::work_1);
	auto const default_receive_difficulty = node.default_receive_difficulty (nano::work_version::work_1);
	auto const receive_current_denormalized = node.network_params.work.denormalized_multiplier (multiplier_active, node.network_params.work.epoch_2_receive);

	boost::json::object data;
	data["deprecated"] = "1";
	data["network_minimum"] = nano::to_string_hex (default_difficulty);
	data["network_receive_minimum"] = nano::to_string_hex (default_receive_difficulty);
	data["network_current"] = nano::to_string_hex (nano::difficulty::from_multiplier (multiplier_active, default_difficulty));
	data["network_receive_current"] = nano::to_string_hex (nano::difficulty::from_multiplier (receive_current_denormalized, default_receive_difficulty));
	data["multiplier"] = 1.0;

	if (include_trend)
	{
		// To keep this RPC backwards-compatible, return hardcoded trend
		boost::json::array difficulty_trend_array;
		difficulty_trend_array.push_back (boost::json::value ("1.000000000000000"));
		data["difficulty_trend"] = difficulty_trend_array;
	}

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_delegators_count (boost::json::object const & request)
{
	// Validate required field: account
	if (!request.contains ("account"))
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	auto const & account_value = request.at ("account");
	if (!account_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	std::string account_text = std::string (account_value.as_string ());
	nano::account account;

	if (account.decode_account (account_text))
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	uint64_t count = 0;
	auto transaction = node.ledger.tx_begin_read ();
	for (auto i = node.store.account.begin (transaction), n = node.store.account.end (transaction); i != n; ++i)
	{
		nano::account_info const & info = i->second;
		if (info.representative == account)
		{
			++count;
		}
	}

	boost::json::object data;
	data["count"] = std::to_string (count);

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_delegators (boost::json::object const & request)
{
	// Validate required field: account (representative)
	if (!request.contains ("account"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: account");
	}

	auto const & account_value = request.at ("account");
	if (!account_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'account' must be a string");
	}

	std::string account_text = std::string (account_value.as_string ());
	nano::account representative;

	if (representative.decode_account (account_text))
	{
		return nano::rpc::v3::response_builder::error ("Bad account number");
	}

	// Parse optional count parameter (default: 1024)
	uint64_t count = 1024;
	if (request.contains ("count"))
	{
		auto const & count_value = request.at ("count");
		if (count_value.is_string ())
		{
			std::string count_text = std::string (count_value.as_string ());
			std::stringstream ss (count_text);
			ss >> count;
			if (ss.fail () || count == 0)
			{
				return nano::rpc::v3::response_builder::error ("Invalid count");
			}
		}
		else if (count_value.is_int64 ())
		{
			count = count_value.as_int64 ();
		}
	}

	// Parse optional threshold parameter (default: 0)
	nano::amount threshold (0);
	if (request.contains ("threshold"))
	{
		auto const & threshold_value = request.at ("threshold");
		if (threshold_value.is_string ())
		{
			std::string threshold_text = std::string (threshold_value.as_string ());
			if (threshold.decode_dec (threshold_text))
			{
				return nano::rpc::v3::response_builder::error ("Bad threshold");
			}
		}
	}

	// Parse optional start account parameter
	nano::account start_account{};
	if (request.contains ("start"))
	{
		auto const & start_value = request.at ("start");
		if (!start_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'start' must be a string");
		}
		std::string start_text = std::string (start_value.as_string ());
		if (start_account.decode_account (start_text))
		{
			return nano::rpc::v3::response_builder::error ("Invalid start account");
		}
	}

	// Get delegators from ledger
	auto transaction = node.store.tx_begin_read ();
	boost::json::object delegators_obj;

	// Helper function to increment account number (saturating)
	auto inc_sat = [] (nano::uint256_t const & value) -> nano::uint256_t {
		nano::uint256_t result = value + 1;
		return result < value ? value : result;
	};

	for (auto i = node.store.account.begin (transaction, inc_sat (start_account.number ())), n = node.store.account.end (transaction); i != n && delegators_obj.size () < count; ++i)
	{
		nano::account_info const & info = i->second;
		if (info.representative == representative)
		{
			if (info.balance.number () >= threshold.number ())
			{
				nano::account const & delegator = i->first;
				delegators_obj[delegator.to_account ()] = nano::amount{ info.balance.number () }.to_string_dec ();
			}
		}
	}

	boost::json::object data;
	data["delegators"] = delegators_obj;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_confirmation_quorum (boost::json::object const & request)
{
	// Parse optional peer_details parameter
	bool const peer_details = request.contains ("peer_details") && request.at ("peer_details").is_bool () ? request.at ("peer_details").as_bool () : false;

	boost::json::object data;
	data["quorum_delta"] = node.online_reps.delta ().convert_to<std::string> ();
	data["online_weight_quorum_percent"] = std::to_string (node.online_reps.online_weight_quorum);
	data["online_weight_minimum"] = node.config.online_weight_minimum.to_string_dec ();
	data["online_stake_total"] = node.online_reps.online ().convert_to<std::string> ();
	data["trended_stake_total"] = node.online_reps.trended ().convert_to<std::string> ();
	data["peers_stake_total"] = node.rep_crawler.total_weight ().convert_to<std::string> ();

	if (peer_details)
	{
		boost::json::array peers_array;
		for (auto const & peer : node.rep_crawler.representatives ())
		{
			boost::json::object peer_obj;
			peer_obj["account"] = peer.account.to_account ();
			peer_obj["ip"] = peer.channel->to_string ();
			peer_obj["weight"] = nano::amount{ node.ledger.weight (peer.account) }.to_string_dec ();
			peers_array.push_back (peer_obj);
		}
		data["peers"] = peers_array;
	}

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_confirmation_info (boost::json::object const & request)
{
	// Validate required field: root
	if (!request.contains ("root"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: root");
	}

	auto const & root_value = request.at ("root");
	if (!root_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'root' must be a string");
	}

	std::string root_text = std::string (root_value.as_string ());
	nano::qualified_root root;

	if (root.decode_hex (root_text))
	{
		return nano::rpc::v3::response_builder::error ("Invalid root");
	}

	// Parse optional parameters
	bool const representatives = request.contains ("representatives") && request.at ("representatives").is_bool () ? request.at ("representatives").as_bool () : false;
	bool const contents = request.contains ("contents") && request.at ("contents").is_bool () ? request.at ("contents").as_bool () : true;
	bool const json_block = request.contains ("json_block") && request.at ("json_block").is_bool () ? request.at ("json_block").as_bool () : false;

	// Get election from active elections
	auto election = node.active.election (root);

	if (election == nullptr || election->confirmed ())
	{
		return nano::rpc::v3::response_builder::error ("Confirmation not found");
	}

	auto info = election->current_status ();

	boost::json::object data;
	data["announcements"] = std::to_string (info.status.confirmation_request_count);
	data["voters"] = std::to_string (info.votes.size ());
	data["last_winner"] = info.status.winner->hash ().to_string ();

	nano::uint128_t total (0);
	boost::json::object blocks_obj;

	for (auto const & [tally, block] : info.tally)
	{
		boost::json::object entry;
		entry["tally"] = tally.convert_to<std::string> ();
		total += tally;

		if (contents)
		{
			if (json_block)
			{
				entry["contents"] = block->to_json ();
			}
			else
			{
				std::string block_json_str;
				block->serialize_json (block_json_str);
				entry["contents"] = block_json_str;
			}
		}

		if (representatives)
		{
			std::multimap<nano::uint128_t, nano::account, std::greater<nano::uint128_t>> reps;
			std::multimap<nano::uint128_t, nano::account, std::greater<nano::uint128_t>> reps_final;

			for (auto const & [rep, vote] : info.votes)
			{
				if (block->hash () == vote.hash)
				{
					auto amount = node.ledger.weight (rep);
					reps.emplace (amount, rep);
					if (vote.timestamp == std::numeric_limits<uint64_t>::max ())
					{
						reps_final.emplace (amount, rep);
					}
				}
			}

			boost::json::object reps_obj;
			boost::json::object reps_final_obj;

			for (auto const & [amount, rep] : reps)
			{
				reps_obj[rep.to_account ()] = amount.convert_to<std::string> ();
			}
			for (auto const & [amount, rep] : reps_final)
			{
				reps_final_obj[rep.to_account ()] = amount.convert_to<std::string> ();
			}

			entry["representatives"] = reps_obj;
			entry["representatives_final"] = reps_final_obj;
		}

		blocks_obj[block->hash ().to_string ()] = entry;
	}

	data["total_tally"] = total.convert_to<std::string> ();
	data["final_tally"] = info.status.final_tally.to_string_dec ();
	data["blocks"] = blocks_obj;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_unchecked (boost::json::object const & request)
{
	// Parse optional count parameter (default: all)
	uint64_t count = std::numeric_limits<uint64_t>::max ();
	if (request.contains ("count"))
	{
		auto const & count_value = request.at ("count");
		if (count_value.is_string ())
		{
			std::string count_text = std::string (count_value.as_string ());
			std::stringstream ss (count_text);
			ss >> count;
			if (ss.fail () || count == 0)
			{
				return nano::rpc::v3::response_builder::error ("Invalid count");
			}
		}
		else if (count_value.is_int64 ())
		{
			count = count_value.as_int64 ();
		}
	}

	// Parse optional json_block parameter
	bool const json_block = request.contains ("json_block") && request.at ("json_block").is_bool () ? request.at ("json_block").as_bool () : false;

	// Get unchecked blocks
	boost::json::object blocks_obj;

	node.unchecked.for_each (
	[&blocks_obj, json_block] (nano::unchecked_key const & key, nano::unchecked_info const & info) {
		if (json_block)
		{
			blocks_obj[info.block->hash ().to_string ()] = info.block->to_json ();
		}
		else
		{
			std::string contents;
			info.block->serialize_json (contents);
			blocks_obj[info.block->hash ().to_string ()] = contents;
		} }, [iterations = 0, count = count] () mutable { return iterations++ < count; });

	boost::json::object data;
	data["blocks"] = blocks_obj;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_unchecked_get (boost::json::object const & request)
{
	// Validate required field: hash
	if (!request.contains ("hash"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: hash");
	}

	auto const & hash_value = request.at ("hash");
	if (!hash_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'hash' must be a string");
	}

	std::string hash_text = std::string (hash_value.as_string ());
	nano::block_hash hash;

	if (hash.decode_hex (hash_text))
	{
		return nano::rpc::v3::response_builder::error ("Invalid block hash");
	}

	// Parse optional json_block parameter
	bool const json_block = request.contains ("json_block") && request.at ("json_block").is_bool () ? request.at ("json_block").as_bool () : false;

	// Search for the unchecked block
	boost::json::object data;
	bool found = false;

	node.unchecked.for_each (
	[&] (nano::unchecked_key const & key, nano::unchecked_info const & info) {
		if (key.hash == hash)
		{
			data["modified_timestamp"] = std::to_string (info.modified ());

			if (json_block)
			{
				data["contents"] = info.block->to_json ();
			}
			else
			{
				std::string contents;
				info.block->serialize_json (contents);
				data["contents"] = contents;
			}
			found = true;
		} }, [&] () { return !found; });

	if (!found)
	{
		return nano::rpc::v3::response_builder::error ("Block not found");
	}

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_unchecked_keys (boost::json::object const & request)
{
	// Parse optional count parameter
	uint64_t count = std::numeric_limits<uint64_t>::max ();
	if (request.contains ("count"))
	{
		auto const & count_value = request.at ("count");
		if (count_value.is_string ())
		{
			std::string count_text = std::string (count_value.as_string ());
			std::stringstream ss (count_text);
			ss >> count;
			if (ss.fail () || count == 0)
			{
				return nano::rpc::v3::response_builder::error ("Invalid count");
			}
		}
		else if (count_value.is_int64 ())
		{
			count = count_value.as_int64 ();
		}
	}

	// Parse optional json_block parameter
	bool const json_block = request.contains ("json_block") && request.at ("json_block").is_bool () ? request.at ("json_block").as_bool () : false;

	// Parse optional key parameter (starting hash)
	nano::block_hash start_key (0);
	if (request.contains ("key"))
	{
		auto const & key_value = request.at ("key");
		if (!key_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'key' must be a string");
		}
		std::string key_text = std::string (key_value.as_string ());
		if (start_key.decode_hex (key_text))
		{
			return nano::rpc::v3::response_builder::error ("Bad key");
		}
	}

	// Get unchecked keys
	boost::json::array unchecked_array;

	node.unchecked.for_each (
	[&] (nano::unchecked_key const & key, nano::unchecked_info const & info) {
		if (key.hash >= start_key)
		{
			boost::json::object entry;
			entry["key"] = key.hash.to_string ();
			entry["hash"] = info.block->hash ().to_string ();
			entry["modified_timestamp"] = std::to_string (info.modified ());

			if (json_block)
			{
				entry["contents"] = info.block->to_json ();
			}
			else
			{
				std::string contents;
				info.block->serialize_json (contents);
				entry["contents"] = contents;
			}

			unchecked_array.push_back (entry);
		} }, [iterations = 0, count = count] () mutable { return iterations++ < count; });

	boost::json::object data;
	data["unchecked"] = unchecked_array;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_pruned_exists (boost::json::object const & request)
{
	// Validate required field: hash
	if (!request.contains ("hash"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: hash");
	}

	auto const & hash_value = request.at ("hash");
	if (!hash_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'hash' must be a string");
	}

	std::string hash_text = std::string (hash_value.as_string ());
	nano::block_hash hash;

	if (hash.decode_hex (hash_text))
	{
		return nano::rpc::v3::response_builder::error ("Invalid block hash");
	}

	auto transaction = node.store.tx_begin_read ();

	if (!node.ledger.pruning)
	{
		return nano::rpc::v3::response_builder::error ("Pruning not enabled");
	}

	auto exists = node.store.pruned.exists (transaction, hash);

	boost::json::object data;
	data["exists"] = exists ? "1" : "0";

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_pending (boost::json::object const & request)
{
	// Mark as deprecated and forward to receivable
	auto result = handle_receivable (request);

	// Add deprecated flag if not already an error
	if (!result.contains ("error"))
	{
		result["deprecated"] = "1";
	}

	return result;
}

boost::json::object rpc_v3_handler::handle_pending_exists (boost::json::object const & request)
{
	// Mark as deprecated and forward to receivable_exists
	auto result = handle_receivable_exists (request);

	// Add deprecated flag if not already an error
	if (!result.contains ("error"))
	{
		result["deprecated"] = "1";
	}

	return result;
}

boost::json::object rpc_v3_handler::handle_work_validate (boost::json::object const & request)
{
	// Validate required field: work
	if (!request.contains ("work"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: work");
	}

	// Validate required field: hash
	if (!request.contains ("hash"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: hash");
	}

	auto const & work_value = request.at ("work");
	if (!work_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'work' must be a string");
	}

	auto const & hash_value = request.at ("hash");
	if (!hash_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'hash' must be a string");
	}

	std::string work_text = std::string (work_value.as_string ());
	std::string hash_text = std::string (hash_value.as_string ());

	uint64_t work;
	if (nano::from_string_hex (work_text, work))
	{
		return nano::rpc::v3::response_builder::error ("Bad work");
	}

	nano::block_hash hash;
	if (hash.decode_hex (hash_text))
	{
		return nano::rpc::v3::response_builder::error ("Bad hash");
	}

	// Parse optional version parameter (default: work_1)
	nano::work_version work_version = nano::work_version::work_1;
	if (request.contains ("version"))
	{
		auto const & version_value = request.at ("version");
		if (version_value.is_string ())
		{
			std::string version_text = std::string (version_value.as_string ());
			if (version_text == "work_1")
			{
				work_version = nano::work_version::work_1;
			}
			else
			{
				return nano::rpc::v3::response_builder::error ("Invalid work version");
			}
		}
	}

	// Parse optional difficulty parameter
	std::optional<uint64_t> difficulty_opt;
	if (request.contains ("difficulty"))
	{
		auto const & difficulty_value = request.at ("difficulty");
		if (difficulty_value.is_string ())
		{
			std::string difficulty_text = std::string (difficulty_value.as_string ());
			uint64_t difficulty;
			if (nano::from_string_hex (difficulty_text, difficulty))
			{
				return nano::rpc::v3::response_builder::error ("Bad difficulty");
			}
			difficulty_opt = difficulty;
		}
	}

	// Calculate the actual difficulty
	auto result_difficulty = node.network_params.work.difficulty (work_version, hash, work);

	boost::json::object data;

	// If difficulty was specified, validate against it
	if (difficulty_opt)
	{
		data["valid"] = (result_difficulty >= difficulty_opt.value ()) ? "1" : "0";
	}

	// Always include these fields
	data["valid_all"] = (result_difficulty >= node.default_difficulty (work_version)) ? "1" : "0";
	data["valid_receive"] = (result_difficulty >= node.network_params.work.threshold (work_version, nano::block_details (nano::epoch::epoch_2, false, true, false))) ? "1" : "0";
	data["difficulty"] = nano::to_string_hex (result_difficulty);

	auto result_multiplier = nano::difficulty::to_multiplier (result_difficulty, node.default_difficulty (work_version));
	data["multiplier"] = nano::to_string (result_multiplier);

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_work_generate (boost::json::object const & request)
{
	// Validate required field: hash
	if (!request.contains ("hash"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: hash");
	}

	auto const & hash_value = request.at ("hash");
	if (!hash_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error ("Field 'hash' must be a string");
	}

	std::string hash_text = std::string (hash_value.as_string ());
	nano::block_hash hash;

	if (hash.decode_hex (hash_text))
	{
		return nano::rpc::v3::response_builder::error ("Bad hash");
	}

	// Parse optional version parameter (default: work_1)
	nano::work_version work_version = nano::work_version::work_1;
	if (request.contains ("version"))
	{
		auto const & version_value = request.at ("version");
		if (version_value.is_string ())
		{
			std::string version_text = std::string (version_value.as_string ());
			if (version_text == "work_1")
			{
				work_version = nano::work_version::work_1;
			}
			else
			{
				return nano::rpc::v3::response_builder::error ("Invalid work version");
			}
		}
	}

	// Parse optional difficulty parameter
	uint64_t difficulty = node.default_difficulty (work_version);
	if (request.contains ("difficulty"))
	{
		auto const & difficulty_value = request.at ("difficulty");
		if (difficulty_value.is_string ())
		{
			std::string difficulty_text = std::string (difficulty_value.as_string ());
			if (nano::from_string_hex (difficulty_text, difficulty))
			{
				return nano::rpc::v3::response_builder::error ("Bad difficulty");
			}
		}
	}

	// Check difficulty limits
	if (difficulty > node.max_work_generate_difficulty (work_version) ||
	    difficulty < node.network_params.work.threshold_entry (work_version, nano::block_type::state))
	{
		return nano::rpc::v3::response_builder::error ("Difficulty outside allowed range");
	}

	// Parse optional use_peers parameter
	bool const use_peers = request.contains ("use_peers") && request.at ("use_peers").is_bool () ? request.at ("use_peers").as_bool () : false;

	// Note: work_generate is asynchronous in v1, but for v3 we'll make it synchronous for simplicity
	// A real implementation would need async support with callbacks
	auto work_opt = node.work_generate_blocking (work_version, hash, difficulty);

	if (!work_opt)
	{
		return nano::rpc::v3::response_builder::error ("Failed to generate work");
	}

	boost::json::object data;
	data["work"] = nano::to_string_hex (work_opt.value ());
	data["difficulty"] = nano::to_string_hex (node.network_params.work.difficulty (work_version, hash, work_opt.value ()));
	data["multiplier"] = nano::to_string (nano::difficulty::to_multiplier (node.network_params.work.difficulty (work_version, hash, work_opt.value ()), node.default_difficulty (work_version)));
	data["hash"] = hash.to_string ();

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_sign (boost::json::object const & request)
{
	// Parse optional json_block parameter
	bool const json_block = request.contains ("json_block") && request.at ("json_block").is_bool () ? request.at ("json_block").as_bool () : false;

	// Determine what we're signing - hash or block
	nano::block_hash hash{};
	std::shared_ptr<nano::block> block;

	if (request.contains ("hash"))
	{
		auto const & hash_value = request.at ("hash");
		if (!hash_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'hash' must be a string");
		}

		std::string hash_text = std::string (hash_value.as_string ());
		if (hash.decode_hex (hash_text))
		{
			return nano::rpc::v3::response_builder::error ("Bad hash");
		}

		// Check if enable_sign_hash is enabled
		if (!config.enable_sign_hash)
		{
			return nano::rpc::v3::response_builder::error ("Signing hash is disabled");
		}
	}

	if (request.contains ("block"))
	{
		auto const & block_value = request.at ("block");
		std::string block_text;

		if (block_value.is_string ())
		{
			block_text = std::string (block_value.as_string ());
		}
		else if (block_value.is_object ())
		{
			block_text = boost::json::serialize (block_value);
		}
		else
		{
			return nano::rpc::v3::response_builder::error ("Invalid block format");
		}

		// Parse the block
		std::stringstream block_stream (block_text);
		boost::property_tree::ptree block_tree;
		try
		{
			boost::property_tree::read_json (block_stream, block_tree);
			block = nano::deserialize_block_json (block_tree);
		}
		catch (...)
		{
			return nano::rpc::v3::response_builder::error ("Invalid block");
		}

		if (block == nullptr)
		{
			return nano::rpc::v3::response_builder::error ("Invalid block");
		}

		hash = block->hash ();
	}

	if (hash.is_zero ())
	{
		return nano::rpc::v3::response_builder::error ("Missing hash or block");
	}

	// Get the private key
	nano::raw_key prv;
	prv.clear ();

	if (request.contains ("key"))
	{
		auto const & key_value = request.at ("key");
		if (!key_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'key' must be a string");
		}

		std::string key_text = std::string (key_value.as_string ());
		if (prv.decode_hex (key_text))
		{
			return nano::rpc::v3::response_builder::error ("Bad private key");
		}
	}
	else if (request.contains ("wallet") && request.contains ("account"))
	{
		// Wallet signing not implemented in this basic version
		return nano::rpc::v3::response_builder::error ("Wallet signing not yet implemented in v3");
	}
	else
	{
		return nano::rpc::v3::response_builder::error ("Missing key or wallet/account");
	}

	// Sign the hash
	nano::public_key pub (nano::pub_key (prv));
	nano::signature signature = nano::sign_message (prv, pub, hash);

	boost::json::object data;
	data["signature"] = signature.to_string ();
	data["block"] = hash.to_string ();

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_process (boost::json::object const & request)
{
	// Validate required field: block
	if (!request.contains ("block"))
	{
		return nano::rpc::v3::response_builder::error ("Missing required field: block");
	}

	auto const & block_value = request.at ("block");
	std::string block_text;

	if (block_value.is_string ())
	{
		block_text = std::string (block_value.as_string ());
	}
	else if (block_value.is_object ())
	{
		block_text = boost::json::serialize (block_value);
	}
	else
	{
		return nano::rpc::v3::response_builder::error ("Invalid block format");
	}

	// Parse the block
	std::shared_ptr<nano::block> block;
	std::stringstream block_stream (block_text);
	boost::property_tree::ptree block_tree;

	try
	{
		boost::property_tree::read_json (block_stream, block_tree);
		block = nano::deserialize_block_json (block_tree);
	}
	catch (...)
	{
		return nano::rpc::v3::response_builder::error ("Invalid block JSON");
	}

	if (block == nullptr)
	{
		return nano::rpc::v3::response_builder::error ("Invalid block");
	}

	// Parse optional subtype for state blocks
	if (block->type () == nano::block_type::state && request.contains ("subtype"))
	{
		auto const & subtype_value = request.at ("subtype");
		if (subtype_value.is_string ())
		{
			std::string subtype_text = std::string (subtype_value.as_string ());
			auto state_block = std::static_pointer_cast<nano::state_block> (block);

			auto transaction = node.ledger.tx_begin_read ();

			// Validate subtype
			if (!state_block->hashables.previous.is_zero () && !node.ledger.any.block_exists (transaction, state_block->hashables.previous))
			{
				return nano::rpc::v3::response_builder::error ("Gap previous block");
			}

			auto balance = node.ledger.any.account_balance (transaction, state_block->hashables.account).value_or (0).number ();

			if (subtype_text == "send")
			{
				if (balance <= state_block->hashables.balance.number ())
				{
					return nano::rpc::v3::response_builder::error ("Invalid subtype balance");
				}
			}
			else if (subtype_text == "receive")
			{
				if (balance > state_block->hashables.balance.number ())
				{
					return nano::rpc::v3::response_builder::error ("Invalid subtype balance");
				}
			}
			else if (subtype_text == "open")
			{
				if (!state_block->hashables.previous.is_zero ())
				{
					return nano::rpc::v3::response_builder::error ("Invalid subtype previous");
				}
			}
			else if (subtype_text == "change")
			{
				if (balance != state_block->hashables.balance.number ())
				{
					return nano::rpc::v3::response_builder::error ("Invalid subtype balance");
				}
				if (state_block->hashables.previous.is_zero ())
				{
					return nano::rpc::v3::response_builder::error ("Invalid subtype previous");
				}
			}
			else if (subtype_text == "epoch")
			{
				if (balance != state_block->hashables.balance.number ())
				{
					return nano::rpc::v3::response_builder::error ("Invalid subtype balance");
				}
			}
		}
	}

	// Process the block
	auto result_maybe = node.process_local (block);

	if (!result_maybe)
	{
		return nano::rpc::v3::response_builder::error ("Node stopped");
	}

	auto const & result = result_maybe.value ();

	boost::json::object data;
	data["hash"] = block->hash ().to_string ();

	// Map result code to string
	switch (result)
	{
		case nano::block_status::progress:
			data["result"] = "progress";
			break;
		case nano::block_status::gap_previous:
			data["result"] = "gap_previous";
			break;
		case nano::block_status::gap_source:
			data["result"] = "gap_source";
			break;
		case nano::block_status::old:
			data["result"] = "old";
			break;
		case nano::block_status::bad_signature:
			data["result"] = "bad_signature";
			break;
		case nano::block_status::insufficient_work:
			data["result"] = "insufficient_work";
			break;
		case nano::block_status::fork:
			data["result"] = "fork";
			break;
		default:
			data["result"] = "error";
			break;
	}

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_stats (boost::json::object const & request)
{
	// Get the optional type parameter (default to all if not specified)
	std::string type;
	if (request.contains ("type"))
	{
		if (!request.at ("type").is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'type' must be a string");
		}
		type = std::string (request.at ("type").as_string ());
	}

	boost::json::object data;

	// Helper lambda to convert ptree to boost::json::value
	auto ptree_to_json = [] (boost::property_tree::ptree const & pt) -> boost::json::value {
		std::stringstream ss;
		boost::property_tree::write_json (ss, pt);
		return boost::json::parse (ss.str ());
	};

	// Add stat duration to response
	auto stat_duration_seconds = node.stats.last_reset ().count ();

	if (type == "counters")
	{
		nano::stat_json_writer sink;
		node.stats.log_counters (sink);
		auto stat_ptree = sink.to_ptree ();
		stat_ptree.put ("stat_duration_seconds", stat_duration_seconds);

		// Convert ptree to boost::json
		auto json_value = ptree_to_json (stat_ptree);
		if (json_value.is_object ())
		{
			data = json_value.as_object ();
		}
	}
	else if (type == "samples")
	{
		nano::stat_json_writer sink;
		node.stats.log_samples (sink);
		auto stat_ptree = sink.to_ptree ();
		stat_ptree.put ("stat_duration_seconds", stat_duration_seconds);

		// Convert ptree to boost::json
		auto json_value = ptree_to_json (stat_ptree);
		if (json_value.is_object ())
		{
			data = json_value.as_object ();
		}
	}
	else if (type == "objects")
	{
		// Note: "objects" type requires a complex recursive construction from container_info
		// For simplicity in v3, we'll skip this for now and focus on the other stat types
		return nano::rpc::v3::response_builder::error ("Type 'objects' is not yet supported in v3 API. Use 'counters', 'samples', or 'database' instead.");
	}
	else if (type == "database")
	{
		boost::property_tree::ptree database_ptree;
		node.store.serialize_memory_stats (database_ptree);
		database_ptree.put ("stat_duration_seconds", stat_duration_seconds);

		// Convert ptree to boost::json
		auto json_value = ptree_to_json (database_ptree);
		if (json_value.is_object ())
		{
			data = json_value.as_object ();
		}
	}
	else
	{
		return nano::rpc::v3::response_builder::error ("Invalid or missing type. Must be one of: counters, samples, objects, database");
	}

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_telemetry (boost::json::object const & request)
{
	// Helper lambda to convert ptree to boost::json::value
	auto ptree_to_json = [] (boost::property_tree::ptree const & pt) -> boost::json::value {
		std::stringstream ss;
		boost::property_tree::write_json (ss, pt);
		return boost::json::parse (ss.str ());
	};

	// Helper to convert telemetry_data to boost::json::object
	auto telemetry_to_json = [&ptree_to_json] (nano::telemetry_data const & telemetry_data) -> boost::json::object {
		nano::jsonconfig config_l;
		auto const should_ignore_identification_metrics = false;
		auto err = telemetry_data.serialize_json (config_l, should_ignore_identification_metrics);

		if (err)
		{
			return boost::json::object{}; // Return empty object on error
		}

		auto const & ptree = config_l.get_tree ();
		auto json_value = ptree_to_json (ptree);

		if (json_value.is_object ())
		{
			return json_value.as_object ();
		}

		return boost::json::object{};
	};

	// Check if address and port are provided
	bool has_address = request.contains ("address");
	bool has_port = request.contains ("port");

	if (has_address || has_port)
	{
		// Both address and port must be specified together
		if (!has_address || !has_port)
		{
			return nano::rpc::v3::response_builder::error ("Both 'address' and 'port' must be specified together");
		}

		// Get address and port strings
		if (!request.at ("address").is_string () || !request.at ("port").is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Fields 'address' and 'port' must be strings");
		}

		std::string address_text = std::string (request.at ("address").as_string ());
		std::string port_text = std::string (request.at ("port").as_string ());

		// Parse port
		uint16_t port;
		if (nano::parse_port (port_text, port))
		{
			return nano::rpc::v3::response_builder::error ("Invalid port");
		}

		// Parse address
		boost::asio::ip::address address;
		if (nano::parse_address (address_text, address))
		{
			return nano::rpc::v3::response_builder::error ("Invalid IP address");
		}

		nano::endpoint endpoint{ address, port };

		// Check if requesting local telemetry
		if (address.is_loopback () && port == node.network.endpoint ().port ())
		{
			auto telemetry_data = node.local_telemetry ();
			auto data = telemetry_to_json (telemetry_data);

			if (data.empty ())
			{
				return nano::rpc::v3::response_builder::error ("Failed to serialize telemetry data");
			}

			return nano::rpc::v3::response_builder::success (data);
		}
		else
		{
			// Get telemetry for specific peer
			auto maybe_telemetry = node.telemetry.get_telemetry (nano::transport::map_endpoint_to_v6 (endpoint));

			if (!maybe_telemetry)
			{
				return nano::rpc::v3::response_builder::error ("Peer not found or telemetry not available");
			}

			auto data = telemetry_to_json (*maybe_telemetry);

			if (data.empty ())
			{
				return nano::rpc::v3::response_builder::error ("Failed to serialize telemetry data");
			}

			return nano::rpc::v3::response_builder::success (data);
		}
	}
	else
	{
		// No address/port specified - return local telemetry (default behavior)
		// Note: v1 has a "raw" parameter to get all telemetries, but for v3 we'll keep it simple
		// and just return local telemetry by default
		auto telemetry_data = node.local_telemetry ();
		auto data = telemetry_to_json (telemetry_data);

		if (data.empty ())
		{
			return nano::rpc::v3::response_builder::error ("Failed to serialize telemetry data");
		}

		return nano::rpc::v3::response_builder::success (data);
	}
}

boost::json::object rpc_v3_handler::handle_confirmation_active (boost::json::object const & request)
{
	// Get optional announcements filter parameter
	uint64_t announcements = 0;
	if (request.contains ("announcements"))
	{
		if (!request.at ("announcements").is_string () && !request.at ("announcements").is_int64 () && !request.at ("announcements").is_uint64 ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'announcements' must be a number or numeric string");
		}

		if (request.at ("announcements").is_string ())
		{
			std::string announcements_text = std::string (request.at ("announcements").as_string ());
			announcements = std::stoull (announcements_text);
		}
		else if (request.at ("announcements").is_int64 ())
		{
			announcements = request.at ("announcements").as_int64 ();
		}
		else
		{
			announcements = request.at ("announcements").as_uint64 ();
		}
	}

	// Get active elections
	boost::json::array confirmations;
	uint64_t confirmed_count = 0;

	auto active_elections = node.active.list_active ();
	for (auto const & election : active_elections)
	{
		if (election->confirmation_request_count >= announcements)
		{
			if (!election->confirmed ())
			{
				confirmations.push_back (boost::json::value (election->qualified_root.to_string ()));
			}
			else
			{
				++confirmed_count;
			}
		}
	}

	boost::json::object data;
	data["confirmations"] = confirmations;
	data["unconfirmed"] = confirmations.size ();
	data["confirmed"] = confirmed_count;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_confirmation_history (boost::json::object const & request)
{
	// Get optional hash filter parameter
	nano::block_hash hash (0);
	if (request.contains ("hash"))
	{
		if (!request.at ("hash").is_string ())
		{
			return nano::rpc::v3::response_builder::error ("Field 'hash' must be a string");
		}

		std::string hash_text = std::string (request.at ("hash").as_string ());
		if (hash.decode_hex (hash_text))
		{
			return nano::rpc::v3::response_builder::error ("Invalid hash");
		}
	}

	// Get recently cemented confirmations
	boost::json::array confirmations;
	std::chrono::milliseconds running_total (0);

	// Default to 2000 for now (same as v1)
	for (auto const & status : node.active.recently_cemented.list (2000))
	{
		if (hash.is_zero () || status.winner->hash () == hash)
		{
			boost::json::object election;
			election["hash"] = status.winner->hash ().to_string ();
			election["duration"] = status.election_duration.count ();
			election["time"] = nano::milliseconds_since_epoch (status.election_end);
			election["tally"] = status.tally.to_string_dec ();
			election["final"] = status.final_tally.to_string_dec ();
			election["blocks"] = std::to_string (status.block_count);
			election["voters"] = std::to_string (status.voter_count);
			election["request_count"] = std::to_string (status.confirmation_request_count);

			confirmations.push_back (election);
		}

		running_total += status.election_duration;
	}

	// Build confirmation stats
	boost::json::object confirmation_stats;
	confirmation_stats["count"] = confirmations.size ();
	if (confirmations.size () >= 1)
	{
		confirmation_stats["average"] = running_total.count () / confirmations.size ();
	}

	boost::json::object data;
	data["confirmation_stats"] = confirmation_stats;
	data["confirmations"] = confirmations;

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_stop (boost::json::object const & request)
{
	// Check if enable_control is set
	if (!rpc_config.enable_control)
	{
		return nano::rpc::v3::response_builder::error ("RPC control is disabled");
	}

	// Call the stop callback to stop the node
	stop_callback ();

	// Return success response
	boost::json::object data;
	data["success"] = "";

	return nano::rpc::v3::response_builder::success (data);
}
}
