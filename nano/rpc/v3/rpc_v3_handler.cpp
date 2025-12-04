#include <nano/rpc/v3/rpc_v3_handler.hpp>

#include <nano/lib/blocks.hpp>
#include <nano/lib/version.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/node.hpp>
#include <nano/node/node_rpc_config.hpp>
#include <nano/rpc/v3/error_codes.hpp>
#include <nano/rpc/v3/response_builder.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>

#include <boost/json.hpp>

#include <sstream>

namespace nano
{
rpc_v3_handler::rpc_v3_handler (nano::node & node_a, nano::node_rpc_config const & config_a) :
	node (node_a),
	config (config_a)
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
	action_handlers["block_info"] = [this] (auto const & req) { return handle_block_info (req); };
	action_handlers["block_count"] = [this] (auto const & req) { return handle_block_count (req); };
	action_handlers["available_supply"] = [this] (auto const & req) { return handle_available_supply (req); };
	action_handlers["peers"] = [this] (auto const & req) { return handle_peers (req); };
	action_handlers["uptime"] = [this] (auto const & req) { return handle_uptime (req); };
	action_handlers["validate_account_number"] = [this] (auto const & req) { return handle_validate_account_number (req); };
	action_handlers["account_representative"] = [this] (auto const & req) { return handle_account_representative (req); };

	// Tier 2 - Lists/Bulk
	action_handlers["accounts_balances"] = [this] (auto const & req) { return handle_accounts_balances (req); };
	action_handlers["blocks_info"] = [this] (auto const & req) { return handle_blocks_info (req); };

	// Tier 3 - State-Modifying
	action_handlers["block_create"] = [this] (auto const & req) { return handle_block_create (req); };
}

void rpc_v3_handler::process_request (
std::string const & body,
std::function<void (std::string const &)> response)
{
	try
	{
		// Parse JSON request
		boost::json::value request_value = boost::json::parse (body);

		// Ensure request is an object
		if (!request_value.is_object ())
		{
			auto error_response = nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
			"Request must be a JSON object");
			response (nano::rpc::v3::response_builder::serialize (error_response));
			return;
		}

		auto & request_obj = request_value.as_object ();

		// Extract action field
		if (!request_obj.contains ("action"))
		{
			auto error_response = nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::MISSING_REQUIRED_FIELD),
			"Missing required field: action");
			response (nano::rpc::v3::response_builder::serialize (error_response));
			return;
		}

		// Get action as string
		auto const & action_value = request_obj.at ("action");
		if (!action_value.is_string ())
		{
			auto error_response = nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
			"Field 'action' must be a string");
			response (nano::rpc::v3::response_builder::serialize (error_response));
			return;
		}

		std::string action = std::string (action_value.as_string ());

		// Look up handler for this action
		auto handler_it = action_handlers.find (action);
		if (handler_it == action_handlers.end ())
		{
			auto error_response = nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::UNKNOWN_ACTION),
			"Unknown action: " + action);
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
		auto error_response = nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INVALID_JSON),
		std::string ("JSON parsing error: ") + e.what ());
		response (nano::rpc::v3::response_builder::serialize (error_response));
	}
	catch (std::exception const & e)
	{
		// General error
		auto error_response = nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INTERNAL_ERROR),
		std::string ("Internal error: ") + e.what ());
		response (nano::rpc::v3::response_builder::serialize (error_response));
	}
}

boost::json::object rpc_v3_handler::handle_version (boost::json::object const & request)
{
	// Build version information
	boost::json::object data;
	data["rpc_version"] = 3;
	data["node_vendor"] = NANO_VERSION_STRING;
	data["protocol_version"] = std::to_string (node.network_params.network.protocol_version);
	data["network"] = node.network_params.network.get_current_network_as_string ();

	return nano::rpc::v3::response_builder::success (data);
}

boost::json::object rpc_v3_handler::handle_account_balance (boost::json::object const & request)
{
	// Validate required field: account
	if (!request.contains ("account"))
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::MISSING_REQUIRED_FIELD),
		"Missing required field: account");
	}

	auto const & account_value = request.at ("account");
	if (!account_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
		"Field 'account' must be a string");
	}

	std::string account_text = std::string (account_value.as_string ());
	nano::account account;

	// Decode account address
	if (account.decode_account (account_text))
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INVALID_ACCOUNT_FORMAT),
		"Invalid account format");
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
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::MISSING_REQUIRED_FIELD),
		"Missing required field: account");
	}

	auto const & account_value = request.at ("account");
	if (!account_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
		"Field 'account' must be a string");
	}

	std::string account_text = std::string (account_value.as_string ());
	nano::account account;

	if (account.decode_account (account_text))
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INVALID_ACCOUNT_FORMAT),
		"Invalid account format");
	}

	// Get account info from ledger
	auto transaction = node.store.tx_begin_read ();
	nano::account_info info;

	if (!node.store.account.get (transaction, account, info))
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::ACCOUNT_NOT_FOUND),
		"Account not found");
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
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::MISSING_REQUIRED_FIELD),
		"Missing required field: hash");
	}

	auto const & hash_value = request.at ("hash");
	if (!hash_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
		"Field 'hash' must be a string");
	}

	std::string hash_text = std::string (hash_value.as_string ());
	nano::block_hash hash;

	if (hash.decode_hex (hash_text))
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INVALID_BLOCK_HASH),
		"Invalid block hash");
	}

	// Get block from ledger
	auto transaction = node.ledger.tx_begin_read ();
	auto block = node.ledger.any.block_get (transaction, hash);

	if (!block)
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::BLOCK_NOT_FOUND),
		"Block not found");
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
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::MISSING_REQUIRED_FIELD),
		"Missing required field: account");
	}

	auto const & account_value = request.at ("account");
	if (!account_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
		"Field 'account' must be a string");
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
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::MISSING_REQUIRED_FIELD),
		"Missing required field: account");
	}

	auto const & account_value = request.at ("account");
	if (!account_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
		"Field 'account' must be a string");
	}

	std::string account_text = std::string (account_value.as_string ());
	nano::account account;

	if (account.decode_account (account_text))
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INVALID_ACCOUNT_FORMAT),
		"Invalid account format");
	}

	// Get account info
	auto transaction = node.store.tx_begin_read ();
	nano::account_info info;

	if (!node.store.account.get (transaction, account, info))
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::ACCOUNT_NOT_FOUND),
		"Account not found");
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
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::MISSING_REQUIRED_FIELD),
		"Missing required field: accounts");
	}

	auto const & accounts_value = request.at ("accounts");
	if (!accounts_value.is_array ())
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
		"Field 'accounts' must be an array");
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
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::MISSING_REQUIRED_FIELD),
		"Missing required field: hashes");
	}

	auto const & hashes_value = request.at ("hashes");
	if (!hashes_value.is_array ())
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
		"Field 'hashes' must be an array");
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
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::MISSING_REQUIRED_FIELD),
		"Missing required field: type");
	}

	auto const & type_value = request.at ("type");
	if (!type_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
		"Field 'type' must be a string");
	}

	std::string type = std::string (type_value.as_string ());

	// Validate required field: key (private key)
	if (!request.contains ("key"))
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::BLOCK_CREATE_KEY_REQUIRED),
		"Missing required field: key (private key)");
	}

	auto const & key_value = request.at ("key");
	if (!key_value.is_string ())
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
		"Field 'key' must be a string");
	}

	std::string key_text = std::string (key_value.as_string ());
	nano::raw_key prv;
	if (prv.decode_hex (key_text))
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::BAD_PRIVATE_KEY),
		"Invalid private key format");
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
				return nano::rpc::v3::response_builder::error (
				std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
				"Invalid work format");
			}
		}
	}

	// Check if work generation is available when work is not provided
	if (work == 0 && !node.work_generation_enabled ())
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::DISABLED_WORK_GENERATION),
		"Work generation is disabled and no work was provided");
	}

	// Parse common optional fields
	nano::account representative{};
	if (request.contains ("representative"))
	{
		auto const & rep_value = request.at ("representative");
		if (!rep_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
			"Field 'representative' must be a string");
		}
		std::string rep_text = std::string (rep_value.as_string ());
		if (representative.decode_account (rep_text))
		{
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::BAD_REPRESENTATIVE_NUMBER),
			"Invalid representative account format");
		}
	}

	nano::account destination{};
	if (request.contains ("destination"))
	{
		auto const & dest_value = request.at ("destination");
		if (!dest_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
			"Field 'destination' must be a string");
		}
		std::string dest_text = std::string (dest_value.as_string ());
		if (destination.decode_account (dest_text))
		{
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::BAD_DESTINATION),
			"Invalid destination account format");
		}
	}

	nano::block_hash source (0);
	if (request.contains ("source"))
	{
		auto const & source_value = request.at ("source");
		if (!source_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
			"Field 'source' must be a string");
		}
		std::string source_text = std::string (source_value.as_string ());
		if (source.decode_hex (source_text))
		{
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::BAD_SOURCE),
			"Invalid source block hash");
		}
	}

	nano::block_hash previous (0);
	if (request.contains ("previous"))
	{
		auto const & prev_value = request.at ("previous");
		if (!prev_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
			"Field 'previous' must be a string");
		}
		std::string prev_text = std::string (prev_value.as_string ());
		if (previous.decode_hex (prev_text))
		{
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::BAD_PREVIOUS),
			"Invalid previous block hash");
		}
	}

	nano::amount balance (0);
	if (request.contains ("balance"))
	{
		auto const & balance_value = request.at ("balance");
		if (!balance_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
			"Field 'balance' must be a string");
		}
		std::string balance_text = std::string (balance_value.as_string ());
		if (balance.decode_dec (balance_text))
		{
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::INVALID_BALANCE),
			"Invalid balance format");
		}
	}

	nano::amount amount (0);
	if (request.contains ("amount"))
	{
		auto const & amount_value = request.at ("amount");
		if (!amount_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
			"Field 'amount' must be a string");
		}
		std::string amount_text = std::string (amount_value.as_string ());
		if (amount.decode_dec (amount_text))
		{
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::INVALID_AMOUNT),
			"Invalid amount format");
		}
	}

	nano::link link (0);
	if (request.contains ("link"))
	{
		auto const & link_value = request.at ("link");
		if (!link_value.is_string ())
		{
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::INVALID_REQUEST_FORMAT),
			"Field 'link' must be a string");
		}
		std::string link_text = std::string (link_value.as_string ());
		if (link.decode_account (link_text))
		{
			if (link.decode_hex (link_text))
			{
				return nano::rpc::v3::response_builder::error (
				std::string (nano::rpc::v3::errors::BAD_LINK),
				"Invalid link format");
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
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::BLOCK_CREATE_REQUIREMENTS_STATE),
			"State block requires: previous, representative, link");
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
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::BLOCK_CREATE_REQUIREMENTS_OPEN),
			"Open block requires: source, representative");
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
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::BLOCK_CREATE_REQUIREMENTS_RECEIVE),
			"Receive block requires: source, previous");
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
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::BLOCK_CREATE_REQUIREMENTS_CHANGE),
			"Change block requires: previous, representative");
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
				return nano::rpc::v3::response_builder::error (
				std::string (nano::rpc::v3::errors::INSUFFICIENT_BALANCE),
				"Insufficient balance for send");
			}
		}
		else
		{
			return nano::rpc::v3::response_builder::error (
			std::string (nano::rpc::v3::errors::BLOCK_CREATE_REQUIREMENTS_SEND),
			"Send block requires: destination, previous, balance, amount");
		}
	}
	else
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INVALID_BLOCK_TYPE),
		"Invalid block type: " + type);
	}

	if (!block || (ec_build && ec_build != nano::error_common::missing_work))
	{
		return nano::rpc::v3::response_builder::error (
		std::string (nano::rpc::v3::errors::INTERNAL_ERROR),
		"Failed to build block");
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
}
