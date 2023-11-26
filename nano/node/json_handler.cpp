#include <nano/lib/config.hpp>
#include <nano/lib/json_error_response.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/bootstrap/bootstrap_lazy.hpp>
#include <nano/node/bootstrap_ascending/service.hpp>
#include <nano/node/common.hpp>
#include <nano/node/election.hpp>
#include <nano/node/json_handler.hpp>
#include <nano/node/node.hpp>
#include <nano/node/node_rpc_config.hpp>
#include <nano/node/telemetry.hpp>

#include <algorithm>
#include <chrono>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{
using ipc_json_handler_no_arg_func_map = std::unordered_map<std::string, std::function<void (nano::json_handler *)>>;
ipc_json_handler_no_arg_func_map create_ipc_json_handler_no_arg_func_map ();
auto ipc_json_handler_no_arg_funcs = create_ipc_json_handler_no_arg_func_map ();
}

nano::json_handler::json_handler (nano::node & node_a, nano::node_rpc_config const & node_rpc_config_a, std::string const & body_a, std::function<void (std::string const &)> const & response_a, std::function<void ()> stop_callback_a) :
	body (body_a),
	node (node_a),
	response (response_a),
	stop_callback (stop_callback_a),
	node_rpc_config (node_rpc_config_a)
{
}

std::function<void ()> nano::json_handler::create_worker_task (std::function<void (std::shared_ptr<nano::json_handler> const &)> const & action_a)
{
	return [rpc_l = shared_from_this (), action_a] () {
		try
		{
			action_a (rpc_l);
		}
		catch (std::runtime_error const &)
		{
			json_error_response (rpc_l->response, "Unable to parse JSON");
		}
		catch (...)
		{
			json_error_response (rpc_l->response, "Internal server error in RPC");
		}
	};
}

void nano::json_handler::process_request (bool unsafe_a)
{
	try
	{
		json_request = nlohmann::json::parse (body);
		action = json_request["action"].get<std::string> ();
		auto no_arg_func_iter = ipc_json_handler_no_arg_funcs.find (action);
		if (no_arg_func_iter != ipc_json_handler_no_arg_funcs.end ())
		{
			no_arg_func_iter->second (this);
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

void nano::json_handler::response_errors ()
{
	if (!ec && json_response.empty ())
	{
		// Return an error code if no response data was given
		ec = nano::error_rpc::empty_response;
	}
	if (ec)
	{
		nlohmann::json error_response;
		error_response["error"] = ec.message ();
		response (error_response.dump (4));
	}
	else
	{
		response (json_response.dump (4));
	}
}

void nano::json_handler::uptime ()
{
	json_response["seconds"] = std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - node.startup_time).count ();
	response_errors ();
}

void nano::json_handler::block_count ()
{
	json_response["count"] = std::to_string (node.ledger.cache.block_count);
	json_response["unchecked"] = std::to_string (node.unchecked.count ());
	json_response["cemented"] = std::to_string (node.ledger.cache.cemented_count);
	if (node.flags.enable_pruning)
	{
		json_response["full"], std::to_string (node.ledger.cache.block_count - node.ledger.cache.pruned_count);
		json_response["pruned"] = std::to_string (node.ledger.cache.pruned_count);
	}
	response_errors ();
}

void nano::json_handler::confirmation_history ()
{
	nlohmann::json elections;
	nlohmann::json confirmation_stats;
	std::chrono::milliseconds running_total (0);
	nano::block_hash hash (0);
	if (!ec)
	{
		for (auto const & status : node.active.recently_cemented.list ())
		{
			if (hash.is_zero () || status.winner->hash () == hash)
			{
				nlohmann::json election;
				election["hash"] = status.winner->hash ().to_string ();
				election["duration"] = status.election_duration.count ();
				election["time"] = status.election_end.count ();
				election["tally"] = status.tally.to_string_dec ();
				election["final"] = status.final_tally.to_string_dec ();
				election["blocks"] = status.block_count;
				election["voters"] = status.voter_count;
				election["request_count"] = status.confirmation_request_count;
				elections.push_back (election);
			}
			running_total += status.election_duration;
		}
	}
	confirmation_stats["count"] = elections.size ();
	if (elections.size () >= 1)
	{
		confirmation_stats["average"] = (running_total.count ()) / elections.size ();
	}

	json_response["confirmation_stats"] = confirmation_stats;
	json_response["confirmations"] = elections;
	response_errors ();
}

void nano::json_handler::deterministic_key ()
{
	std::string seed_text = json_request["seed"].get<std::string> ();
	std::string index_text = json_request["index"].get<std::string> ();

	nano::raw_key seed;
	if (!seed.decode_hex (seed_text))
	{
		try
		{
			uint32_t index (std::stoul (index_text));
			nano::raw_key prv = nano::deterministic_key (seed, index);
			nano::public_key pub (nano::pub_key (prv));
			json_response["private"] = prv.to_string ();
			json_response["public"] = pub.to_string ();
			json_response["account"] = pub.to_account ();
		}
		catch (std::logic_error const &)
		{
			ec = nano::error_common::invalid_index;
		}
	}
	else
	{
		ec = nano::error_common::bad_seed;
	}
	response_errors ();
}

void nano::json_handler::wallet_create ()
{
	node.workers.push_task (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		nano::raw_key seed;

		if (rpc_l->json_request.count ("seed") != 0)
		{
			auto seed_text = rpc_l->json_request["seed"].get<std::string> ();
			if (seed.decode_hex (seed_text))
			{
				rpc_l->ec = nano::error_common::bad_seed;
			}
		}
		if (!rpc_l->ec)
		{
			auto wallet_id = random_wallet_id ();
			auto wallet (rpc_l->node.wallets.create (wallet_id));
			auto existing (rpc_l->node.wallets.items.find (wallet_id));
			if (existing != rpc_l->node.wallets.items.end ())
			{
				rpc_l->json_response["wallet"] = wallet_id.to_string ();
			}
			else
			{
				rpc_l->ec = nano::error_common::wallet_lmdb_max_dbs;
			}
			if (!rpc_l->ec && rpc_l->json_request.count ("seed") != 0)
			{
				auto transaction (rpc_l->node.wallets.tx_begin_write ());
				nano::public_key account (wallet->change_seed (transaction, seed));
				rpc_l->json_response["last_restored_account"] = account.to_account ();
				auto index (wallet->store.deterministic_index_get (transaction));
				debug_assert (index > 0);
				rpc_l->json_response["restored_count"] = std::to_string (index);
			}
		}
		rpc_l->response_errors ();
	}));
}

void nano::json_handler::account_info ()
{
	auto account (account_impl ());
	if (!ec)
	{
		bool const representative = false; //json_request["representative"].get<bool> ();
		bool const weight = false; // json_request["weight"].get<bool> ();
		bool const pending = false; // json_request["pending"].get<bool> ();
		bool const receivable = false; // json_request["receivable"].get<bool> ();
		bool const include_confirmed = false; // json_request["include_confirmed"].get<bool> ();
		auto transaction (node.store.tx_begin_read ());
		auto info (account_info_impl (transaction, account));
		nano::confirmation_height_info confirmation_height_info;
		node.store.confirmation_height.get (transaction, account, confirmation_height_info);
		if (!ec)
		{
			json_response["frontier"] = info.head.to_string ();
			json_response["open_block"] = info.open_block.to_string ();
			json_response["representative_block"] = node.ledger.representative (transaction, info.head).to_string ();
			nano::amount balance_l (info.balance);
			std::string balance;
			balance_l.encode_dec (balance);

			json_response["balance"] = balance;

			nano::amount confirmed_balance_l;
			if (include_confirmed)
			{
				if (info.block_count != confirmation_height_info.height)
				{
					confirmed_balance_l = node.ledger.balance (transaction, confirmation_height_info.frontier);
				}
				else
				{
					// block_height and confirmed height are the same, so can just reuse balance
					confirmed_balance_l = balance_l;
				}
				std::string confirmed_balance;
				confirmed_balance_l.encode_dec (confirmed_balance);
				json_response["confirmed_balance"] = confirmed_balance;
			}

			json_response["modified_timestamp"] = std::to_string (info.modified);
			json_response["block_count"] = std::to_string (info.block_count);
			json_response["epoch_as_string"] = info.epoch ();
			auto confirmed_frontier = confirmation_height_info.frontier.to_string ();
			if (include_confirmed)
			{
				json_response["confirmed_height"] = std::to_string (confirmation_height_info.height);
				json_response["confirmed_frontier"] = confirmed_frontier;
			}
			else
			{
				// For backwards compatibility purposes
				json_response["confirmation_height"] = std::to_string (confirmation_height_info.height);
				json_response["confirmation_height_frontier"] = confirmed_frontier;
			}

			std::shared_ptr<nano::block> confirmed_frontier_block;
			if (include_confirmed && confirmation_height_info.height > 0)
			{
				confirmed_frontier_block = node.store.block.get (transaction, confirmation_height_info.frontier);
			}

			if (representative)
			{
				json_response["representative"] = info.representative.to_account ();
				if (include_confirmed)
				{
					nano::account confirmed_representative{};
					if (confirmed_frontier_block)
					{
						confirmed_representative = confirmed_frontier_block->representative ();
						if (confirmed_representative.is_zero ())
						{
							confirmed_representative = node.store.block.get (transaction, node.ledger.representative (transaction, confirmation_height_info.frontier))->representative ();
						}
					}

					json_response["confirmed_representative"] = confirmed_representative.to_account ();
				}
			}
			if (weight)
			{
				auto account_weight (node.ledger.weight (account));
				json_response["weight"] = account_weight.convert_to<std::string> ();
			}
			if (receivable)
			{
				auto account_receivable = node.ledger.account_receivable (transaction, account);
				json_response["pending"] = account_receivable.convert_to<std::string> ();
				json_response["receivable"] = account_receivable.convert_to<std::string> ();

				if (include_confirmed)
				{
					auto account_receivable = node.ledger.account_receivable (transaction, account, true);
					json_response["confirmed_pending"] = account_receivable.convert_to<std::string> ();
					json_response["confirmed_receivable"] = account_receivable.convert_to<std::string> ();
				}
			}
		}
	}
	response_errors ();
}


void nano::json_handler::wallet_add ()
{
	node.workers.push_task (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		auto wallet (rpc_l->wallet_impl ());
		if (!rpc_l->ec)
		{
			std::string key_text = rpc_l->json_request["key"].get<std::string> ();

			nano::raw_key key;
			if (!key.decode_hex (key_text))
			{
				bool generate_work = false;
				if (rpc_l->json_request.count ("work") > 0)
				{
					generate_work = rpc_l->json_request["work"].get<bool> ();
				}
				auto pub (wallet->insert_adhoc (key, generate_work));
				if (!pub.is_zero ())
				{
					rpc_l->json_response["account"] = pub.to_account ();
				}
				else
				{
					rpc_l->ec = nano::error_common::wallet_locked;
				}
			}
			else
			{
				rpc_l->ec = nano::error_common::bad_private_key;
			}
		}
		rpc_l->response_errors ();
	}));
}

std::shared_ptr<nano::wallet> nano::json_handler::wallet_impl ()
{
	if (!ec)
	{
		std::string wallet_text (json_request["wallet"].get<std::string> ());
		nano::wallet_id wallet;
		if (!wallet.decode_hex (wallet_text))
		{
			if (auto existing = node.wallets.open (wallet); existing != nullptr)
			{
				return existing;
			}
			else
			{
				ec = nano::error_common::wallet_not_found;
			}
		}
		else
		{
			ec = nano::error_common::bad_wallet_number;
		}
	}
	return nullptr;
}

nano::account nano::json_handler::account_impl (std::string account_text, std::error_code ec_a)
{
	nano::account result{};
	if (!ec)
	{
		if (account_text.empty ())
		{
			account_text = (json_request["account"].get<std::string> ());
		}
		if (result.decode_account (account_text))
		{
			ec = ec_a;
		}
		else if (account_text[3] == '-' || account_text[4] == '-')
		{
			// nano- and xrb- prefixes are deprecated
			json_response["deprecated_account_format"] = "1";
		}
	}
	return result;
}

nano::account_info nano::json_handler::account_info_impl (store::transaction const & transaction_a, nano::account const & account_a)
{
	nano::account_info result;
	if (!ec)
	{
		auto info = node.ledger.account_info (transaction_a, account_a);
		if (!info)
		{
			ec = nano::error_common::account_not_found;
			node.bootstrap_initiator.bootstrap_lazy (account_a, false, account_a.to_account ());
		}
		else
		{
			result = *info;
		}
	}
	return result;
}


void nano::inprocess_rpc_handler::process_request (std::string const &, std::string const & body_a, std::function<void (std::string const &)> response_a)
{
	// Note that if the rpc action is async, the shared_ptr<json_handler> lifetime will be extended by the action handler
	auto handler (std::make_shared<nano::json_handler> (node, node_rpc_config, body_a, response_a, [this] () {
		this->stop_callback ();
		this->stop ();
	}));
	handler->process_request ();
}

namespace
{
// Any RPC handlers which require no arguments (excl default arguments) should go here.
// This is to prevent large if/else chains which compilers can have limits for (MSVC for instance has 128).
ipc_json_handler_no_arg_func_map create_ipc_json_handler_no_arg_func_map ()
{
	ipc_json_handler_no_arg_func_map no_arg_funcs;
	no_arg_funcs.emplace ("uptime", &nano::json_handler::uptime);
	no_arg_funcs.emplace ("block_count", &nano::json_handler::block_count);
	no_arg_funcs.emplace ("confirmation_history", &nano::json_handler::confirmation_history);
	no_arg_funcs.emplace ("deterministic_key", &nano::json_handler::deterministic_key);
	no_arg_funcs.emplace ("wallet_create", &nano::json_handler::wallet_create);
	no_arg_funcs.emplace ("wallet_add", &nano::json_handler::wallet_add);
	no_arg_funcs.emplace ("account_info", &nano::json_handler::account_info);
	return no_arg_funcs;
}
}
