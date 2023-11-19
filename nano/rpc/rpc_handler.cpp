#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/json_error_response.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/rpc_handler_interface.hpp>
#include <nano/lib/rpcconfig.hpp>
#include <nano/rpc/rpc_handler.hpp>

#include <unordered_set>

#include <nlohmann/json.hpp>

namespace
{
std::unordered_set<std::string> create_rpc_control_impls ();
std::unordered_set<std::string> rpc_control_impl_set = create_rpc_control_impls ();
}

nano::rpc_handler::rpc_handler (nano::rpc_config const & rpc_config, std::string const & body_a, std::string const & request_id_a, std::function<void (std::string const &)> const & response_a, nano::rpc_handler_interface & rpc_handler_interface_a, nano::logger_mt & logger) :
	body (body_a),
	request_id (request_id_a),
	response (response_a),
	rpc_config (rpc_config),
	rpc_handler_interface (rpc_handler_interface_a),
	logger (logger)
{
}

void nano::rpc_handler::process_request (nano::rpc_handler_request_params const & request_params)
{
	try
	{
		auto max_depth_exceeded (false);
		auto max_depth_possible (0u);
		for (auto ch : body)
		{
			if (ch == '[' || ch == '{')
			{
				if (max_depth_possible >= rpc_config.max_json_depth)
				{
					max_depth_exceeded = true;
					break;
				}
				++max_depth_possible;
			}
		}
		if (max_depth_exceeded)
		{
			json_error_response (response, "Max JSON depth exceeded");
		}
		else
		{
			if (request_params.rpc_version == 1)
			{
				auto json_request = nlohmann::json::parse (body);
				auto action = json_request["action"].get<std::string> ();

				if (rpc_config.rpc_logging.log_rpc)
				{
					// Creating same string via stringstream as using it directly is generating a TSAN warning
					std::stringstream ss;
					ss << request_id;
					logger.always_log (ss.str ());
				}

				// Check if this is a RPC command which requires RPC enabled control
				std::error_code rpc_control_disabled_ec = nano::error_rpc::rpc_control_disabled;

				bool error = false;
				auto found = rpc_control_impl_set.find (action);
				if (found != rpc_control_impl_set.cend () && !rpc_config.enable_control)
				{
					json_error_response (response, rpc_control_disabled_ec.message ());
					error = true;
				}

				if (!error)
				{
					rpc_handler_interface.process_request (action, body, this->response);
				}
			}
			else
			{
				debug_assert (false);
				json_error_response (response, "Invalid RPC version");
			}
		}
	}
	catch (std::runtime_error const &)
	{
		json_error_response (response, "Unable to parse JSON");
	}
	catch (...)
	{
		json_error_response (response, "Internal server error in RPC");
	}
}

namespace
{
std::unordered_set<std::string> create_rpc_control_impls ()
{
	std::unordered_set<std::string> set;
	set.emplace ("account_create");
	set.emplace ("account_move");
	set.emplace ("account_remove");
	set.emplace ("account_representative_set");
	set.emplace ("accounts_create");
	set.emplace ("backoff_info");
	set.emplace ("block_create");
	set.emplace ("bootstrap_lazy");
	set.emplace ("confirmation_height_currently_processing");
	set.emplace ("database_txn_tracker");
	set.emplace ("epoch_upgrade");
	set.emplace ("keepalive");
	set.emplace ("ledger");
	set.emplace ("node_id");
	set.emplace ("password_change");
	set.emplace ("populate_backlog");
	set.emplace ("receive");
	set.emplace ("receive_minimum");
	set.emplace ("receive_minimum_set");
	set.emplace ("search_pending");
	set.emplace ("search_receivable");
	set.emplace ("search_pending_all");
	set.emplace ("search_receivable_all");
	set.emplace ("send");
	set.emplace ("stop");
	set.emplace ("unchecked_clear");
	set.emplace ("unopened");
	set.emplace ("wallet_add");
	set.emplace ("wallet_add_watch");
	set.emplace ("wallet_change_seed");
	set.emplace ("wallet_create");
	set.emplace ("wallet_destroy");
	set.emplace ("wallet_lock");
	set.emplace ("wallet_representative_set");
	set.emplace ("wallet_republish");
	set.emplace ("wallet_work_get");
	set.emplace ("work_generate");
	set.emplace ("work_cancel");
	set.emplace ("work_get");
	set.emplace ("work_set");
	set.emplace ("work_peer_add");
	set.emplace ("work_peers");
	set.emplace ("work_peers_clear");
	set.emplace ("wallet_seed");
	return set;
}


}
