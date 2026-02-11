#include <nano/lib/block_type.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/json_error_response.hpp>
#include <nano/lib/stats_sinks.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/version.hpp>
#include <nano/lib/work_version.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/bootstrap/bootstrap_service.hpp>
#include <nano/node/cementing_set.hpp>
#include <nano/node/election.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/json_handler.hpp>
#include <nano/node/node.hpp>
#include <nano/node/node_rpc_config.hpp>
#include <nano/node/online_reps.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/secure/transaction.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/confirmation_height.hpp>
#include <nano/store/ledger/pending.hpp>
#include <nano/store/ledger/pruned.hpp>

#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <vector>

namespace
{
void construct_json (nano::container_info_component * component, boost::json::object & parent);
using ipc_json_handler_no_arg_func_map = std::unordered_map<std::string, std::function<void (nano::json_handler *)>>;
ipc_json_handler_no_arg_func_map create_ipc_json_handler_no_arg_func_map ();
auto ipc_json_handler_no_arg_funcs = create_ipc_json_handler_no_arg_func_map ();
bool block_confirmed (nano::node & node, nano::secure::transaction & transaction, nano::block_hash const & hash, bool include_active, bool include_only_confirmed);
char const * epoch_as_string (nano::epoch);
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
		request = boost::json::parse (body).as_object ();
		if (node_rpc_config.request_callback)
		{
			debug_assert (node.network_params.network.is_dev_network ());
			node_rpc_config.request_callback (request);
		}
		action = request.at ("action").as_string ().c_str ();
		auto no_arg_func_iter = ipc_json_handler_no_arg_funcs.find (action);
		if (no_arg_func_iter != ipc_json_handler_no_arg_funcs.cend ())
		{
			// First try the map of options with no arguments
			no_arg_func_iter->second (this);
		}
		else
		{
			// Try the rest of the options
			if (action == "wallet_seed")
			{
				if (unsafe_a || node.network_params.network.is_dev_network ())
				{
					wallet_seed ();
				}
				else
				{
					json_error_response (response, "Unsafe RPC not allowed");
				}
			}
			else if (action == "chain")
			{
				chain ();
			}
			else if (action == "successors")
			{
				chain (true);
			}
			else if (action == "history")
			{
				response_l["deprecated"] = "1";
				request["head"] = std::string (request.at ("hash").as_string ().c_str ());
				account_history ();
			}
			else if (action == "nano_to_raw")
			{
				nano_to_raw ();
			}
			else if (action == "raw_to_nano")
			{
				raw_to_nano ();
			}
			else if (action == "password_valid")
			{
				password_valid ();
			}
			else if (action == "wallet_locked")
			{
				password_valid (true);
			}
			else
			{
				json_error_response (response, "Unknown command");
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

void nano::json_handler::response_errors ()
{
	if (!ec && response_l.empty ())
	{
		// Return an error code if no response data was given
		ec = nano::error_rpc::empty_response;
	}
	if (ec)
	{
		boost::json::object response_error;
		response_error["error"] = ec.message ();
		response (boost::json::serialize (response_error));
	}
	else
	{
		response (boost::json::serialize (response_l));
	}
}

std::shared_ptr<nano::wallet> nano::json_handler::wallet_impl ()
{
	if (!ec)
	{
		std::string wallet_text (request.at ("wallet").as_string ().c_str ());
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

bool nano::json_handler::wallet_locked_impl (std::shared_ptr<nano::wallet> const & wallet_a)
{
	bool result (false);
	if (!ec)
	{
		if (wallet_a->is_locked ())
		{
			ec = nano::error_common::wallet_locked;
			result = true;
		}
	}
	return result;
}

bool nano::json_handler::wallet_account_impl (std::shared_ptr<nano::wallet> const & wallet_a, nano::account const & account_a)
{
	bool result (false);
	if (!ec)
	{
		if (wallet_a->exists (account_a))
		{
			result = true;
		}
		else
		{
			ec = nano::error_common::account_not_found_wallet;
		}
	}
	return result;
}

nano::account nano::json_handler::account_impl (std::string account_text, std::error_code ec_a)
{
	nano::account result{};
	if (!ec)
	{
		if (account_text.empty ())
		{
			account_text = request.at ("account").as_string ().c_str ();
		}
		if (result.decode_account (account_text))
		{
			ec = ec_a;
		}
		else if (account_text[3] == '-' || account_text[4] == '-')
		{
			// nano- and xrb- prefixes are deprecated
			response_l["deprecated_account_format"] = "1";
		}
	}
	return result;
}

nano::account_info nano::json_handler::account_info_impl (secure::transaction const & transaction_a, nano::account const & account_a)
{
	nano::account_info result;
	if (!ec)
	{
		auto info = node.ledger.any.account_get (transaction_a, account_a);
		if (!info)
		{
			ec = nano::error_common::account_not_found;
		}
		else
		{
			result = *info;
		}
	}
	return result;
}

nano::amount nano::json_handler::amount_impl ()
{
	nano::amount result (0);
	if (!ec)
	{
		std::string amount_text (request.at ("amount").as_string ().c_str ());
		if (result.decode_dec (amount_text))
		{
			ec = nano::error_common::invalid_amount;
		}
	}
	return result;
}

std::shared_ptr<nano::block> nano::json_handler::block_impl (bool signature_work_required)
{
	bool const json_block_l = request.contains ("json_block") ? request.at ("json_block").as_bool () : false;
	std::shared_ptr<nano::block> result{ nullptr };
	if (!ec)
	{
		boost::json::object block_l;
		if (json_block_l)
		{
			block_l = request.at ("block").as_object ();
		}
		else
		{
			std::string block_text (request.at ("block").as_string ().c_str ());
			try
			{
				block_l = boost::json::parse (block_text).as_object ();
			}
			catch (...)
			{
				ec = nano::error_blocks::invalid_block;
			}
		}
		if (!ec)
		{
			if (!signature_work_required)
			{
				block_l["signature"] = "0";
				block_l["work"] = "0";
			}
			result = nano::deserialize_block_json (block_l);
			if (result == nullptr)
			{
				ec = nano::error_blocks::invalid_block;
			}
		}
	}
	return result;
}

nano::block_hash nano::json_handler::hash_impl (std::string search_text)
{
	nano::block_hash result (0);
	if (!ec)
	{
		std::string hash_text (request.at (search_text).as_string ().c_str ());
		if (result.decode_hex (hash_text))
		{
			ec = nano::error_blocks::invalid_block_hash;
		}
	}
	return result;
}

nano::amount nano::json_handler::threshold_optional_impl ()
{
	nano::amount result (0);
	if (auto* threshold_val = request.if_contains ("threshold"))
	{
		std::string threshold_text (threshold_val->as_string ().c_str ());
		if (!ec && result.decode_dec (threshold_text))
		{
			ec = nano::error_common::bad_threshold;
		}
	}
	return result;
}

uint64_t nano::json_handler::work_optional_impl ()
{
	uint64_t result (0);
	if (auto* work_val = request.if_contains ("work"))
	{
		std::string work_text (work_val->as_string ().c_str ());
		if (!ec && nano::from_string_hex (work_text, result))
		{
			ec = nano::error_common::bad_work_format;
		}
	}
	return result;
}

uint64_t nano::json_handler::difficulty_optional_impl (nano::work_version const version_a)
{
	auto difficulty (node.default_difficulty (version_a));
	if (auto* difficulty_val = request.if_contains ("difficulty"))
	{
		std::string difficulty_text (difficulty_val->as_string ().c_str ());
		if (!ec && nano::from_string_hex (difficulty_text, difficulty))
		{
			ec = nano::error_rpc::bad_difficulty_format;
		}
	}
	return difficulty;
}

uint64_t nano::json_handler::difficulty_ledger (nano::block const & block_a)
{
	nano::block_details details (nano::epoch::epoch_0, false, false, false);
	bool details_found (false);
	auto transaction = node.ledger.tx_begin_read ();
	// Previous block find
	std::shared_ptr<nano::block> block_previous (nullptr);
	auto previous (block_a.previous ());
	if (!previous.is_zero ())
	{
		block_previous = node.ledger.any.block_get (transaction, previous);
	}
	// Send check
	if (block_previous != nullptr)
	{
		details.is_send = node.ledger.any.block_balance (transaction, previous) > block_a.balance_field ().value ().number ();
		details_found = true;
	}
	// Epoch check
	if (block_previous != nullptr)
	{
		details.epoch = block_previous->sideband ().details.epoch;
	}
	auto link = block_a.link_field ();
	if (link && !link.value ().is_zero () && !details.is_send)
	{
		auto block_link = node.ledger.any.block_get (transaction, link.value ().as_block_hash ());
		auto account = block_a.account_field ().value (); // Link is non-zero therefore it's a state block and has an account field;
		if (block_link != nullptr && node.ledger.any.pending_get (transaction, nano::pending_key{ account, link.value ().as_block_hash () }))
		{
			details.epoch = std::max (details.epoch, block_link->sideband ().details.epoch);
			details.is_receive = true;
			details_found = true;
		}
	}
	return details_found ? node.network_params.work.threshold (block_a.work_version (), details) : node.default_difficulty (block_a.work_version ());
}

double nano::json_handler::multiplier_optional_impl (nano::work_version const version_a, uint64_t & difficulty)
{
	double multiplier (1.);
	if (auto* multiplier_val = request.if_contains ("multiplier"))
	{
		std::string multiplier_text (multiplier_val->as_string ().c_str ());
		auto success = boost::conversion::try_lexical_convert<double> (multiplier_text, multiplier);
		if (!ec && success && multiplier > 0.)
		{
			difficulty = nano::difficulty::from_multiplier (multiplier, node.default_difficulty (version_a));
		}
		else if (!ec)
		{
			ec = nano::error_rpc::bad_multiplier_format;
		}
	}
	return multiplier;
}

nano::work_version nano::json_handler::work_version_optional_impl (nano::work_version const default_a)
{
	nano::work_version result = default_a;
	if (auto* version_val = request.if_contains ("version"))
	{
		std::string version_text (version_val->as_string ().c_str ());
		if (!ec)
		{
			if (version_text == nano::to_string (nano::work_version::work_1))
			{
				result = nano::work_version::work_1;
			}
			else
			{
				ec = nano::error_rpc::bad_work_version;
			}
		}
	}
	return result;
}

namespace
{
bool decode_unsigned (std::string const & text, uint64_t & number)
{
	bool result;
	std::size_t end;
	try
	{
		number = std::stoull (text, &end);
		result = false;
	}
	catch (std::invalid_argument const &)
	{
		result = true;
	}
	catch (std::out_of_range const &)
	{
		result = true;
	}
	result = result || end != text.size ();
	return result;
}
}

uint64_t nano::json_handler::count_impl ()
{
	uint64_t result (0);
	if (!ec)
	{
		std::string count_text (request.at ("count").as_string ().c_str ());
		if (decode_unsigned (count_text, result) || result == 0)
		{
			ec = nano::error_common::invalid_count;
		}
	}
	return result;
}

uint64_t nano::json_handler::count_optional_impl (uint64_t result)
{
	if (auto* count_val = request.if_contains ("count"))
	{
		std::string count_text (count_val->as_string ().c_str ());
		if (!ec && decode_unsigned (count_text, result))
		{
			ec = nano::error_common::invalid_count;
		}
	}
	return result;
}

uint64_t nano::json_handler::offset_optional_impl (uint64_t result)
{
	if (auto* offset_val = request.if_contains ("offset"))
	{
		std::string offset_text (offset_val->as_string ().c_str ());
		if (!ec && decode_unsigned (offset_text, result))
		{
			ec = nano::error_rpc::invalid_offset;
		}
	}
	return result;
}

void nano::json_handler::account_balance ()
{
	auto account (account_impl ());
	if (!ec)
	{
		bool const include_only_confirmed = request.contains ("include_only_confirmed") ? request.at ("include_only_confirmed").as_bool () : true;
		auto balance (node.balance_pending (account, include_only_confirmed));
		response_l["balance"] = balance.first.convert_to<std::string> ();
		response_l["pending"] = balance.second.convert_to<std::string> ();
		response_l["receivable"] = balance.second.convert_to<std::string> ();
	}
	response_errors ();
}

void nano::json_handler::account_block_count ()
{
	auto account (account_impl ());
	if (!ec)
	{
		auto transaction = node.ledger.tx_begin_read ();
		auto info (account_info_impl (transaction, account));
		if (!ec)
		{
			response_l["block_count"] = std::to_string (info.block_count);
		}
	}
	response_errors ();
}

void nano::json_handler::account_create ()
{
	node.workers.post (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		auto wallet (rpc_l->wallet_impl ());
		if (!rpc_l->ec)
		{
			bool const generate_work = rpc_l->request.contains ("work") ? rpc_l->request.at ("work").as_bool () : true;
			nano::account new_key;
			if (auto* index_val = rpc_l->request.if_contains ("index"))
			{
				std::string index_text (index_val->as_string ().c_str ());
				uint64_t index;
				if (decode_unsigned (index_text, index) || index > static_cast<uint64_t> (std::numeric_limits<uint32_t>::max ()))
				{
					rpc_l->ec = nano::error_common::invalid_index;
				}
				else
				{
					new_key = wallet->deterministic_insert (static_cast<uint32_t> (index), generate_work);
				}
			}
			else
			{
				new_key = wallet->deterministic_insert (generate_work);
			}

			if (!rpc_l->ec)
			{
				if (!new_key.is_zero ())
				{
					rpc_l->response_l["account"] = new_key.to_account ();
				}
				else
				{
					rpc_l->ec = nano::error_common::wallet_locked;
				}
			}
		}
		rpc_l->response_errors ();
	}));
}

void nano::json_handler::account_get ()
{
	std::string key_text (request.at ("key").as_string ().c_str ());
	nano::public_key pub;
	if (!pub.decode_hex (key_text))
	{
		response_l["account"] = pub.to_account ();
	}
	else
	{
		ec = nano::error_common::bad_public_key;
	}
	response_errors ();
}

void nano::json_handler::account_info ()
{
	auto account (account_impl ());
	if (!ec)
	{
		bool const representative = request.contains ("representative") ? request.at ("representative").as_bool () : false;
		bool const weight = request.contains ("weight") ? request.at ("weight").as_bool () : false;
		bool const pending = request.contains ("pending") ? request.at ("pending").as_bool () : false;
		bool const receivable = request.contains ("receivable") ? request.at ("receivable").as_bool () : pending;
		bool const include_confirmed = request.contains ("include_confirmed") ? request.at ("include_confirmed").as_bool () : false;
		auto transaction = node.ledger.tx_begin_read ();
		auto info (account_info_impl (transaction, account));
		nano::confirmation_height_info confirmation_height_info;
		node.store.confirmation_height.get (transaction, account, confirmation_height_info);
		if (!ec)
		{
			response_l["frontier"] = info.head.to_string ();
			response_l["open_block"] = info.open_block.to_string ();
			response_l["representative_block"] = node.ledger.representative_block (transaction, info.head).to_string ();
			nano::amount balance_l (info.balance);
			std::string balance = balance_l.to_string_dec ();

			response_l["balance"] = balance;

			nano::amount confirmed_balance_l;
			if (include_confirmed)
			{
				if (info.block_count != confirmation_height_info.height)
				{
					confirmed_balance_l = node.ledger.any.block_balance (transaction, confirmation_height_info.frontier).value_or (0);
				}
				else
				{
					// block_height and confirmed height are the same, so can just reuse balance
					confirmed_balance_l = balance_l;
				}
				std::string confirmed_balance = confirmed_balance_l.to_string_dec ();
				response_l["confirmed_balance"] = confirmed_balance;
			}

			response_l["modified_timestamp"] = std::to_string (info.modified);
			response_l["block_count"] = std::to_string (info.block_count);
			response_l["account_version"] = epoch_as_string (info.epoch ());
			auto confirmed_frontier = confirmation_height_info.frontier.to_string ();
			if (include_confirmed)
			{
				response_l["confirmed_height"] = std::to_string (confirmation_height_info.height);
				response_l["confirmed_frontier"] = confirmed_frontier;
			}
			else
			{
				// For backwards compatibility purposes
				response_l["confirmation_height"] = std::to_string (confirmation_height_info.height);
				response_l["confirmation_height_frontier"] = confirmed_frontier;
			}

			std::shared_ptr<nano::block> confirmed_frontier_block;
			if (include_confirmed && confirmation_height_info.height > 0)
			{
				confirmed_frontier_block = node.ledger.any.block_get (transaction, confirmation_height_info.frontier);
			}

			if (representative)
			{
				response_l["representative"] = info.representative.to_account ();
				if (include_confirmed)
				{
					nano::account confirmed_representative{};
					if (confirmed_frontier_block)
					{
						confirmed_representative = confirmed_frontier_block->representative_field ().value_or (0);
						if (confirmed_representative.is_zero ())
						{
							confirmed_representative = node.ledger.any.block_get (transaction, node.ledger.representative_block (transaction, confirmation_height_info.frontier))->representative_field ().value ();
						}
					}

					response_l["confirmed_representative"] = confirmed_representative.to_account ();
				}
			}
			if (weight)
			{
				auto account_weight (node.ledger.weight_exact (transaction, account));
				response_l["weight"] = account_weight.convert_to<std::string> ();
			}
			if (receivable)
			{
				auto account_receivable = node.ledger.account_receivable (transaction, account);
				response_l["pending"] = account_receivable.convert_to<std::string> ();
				response_l["receivable"] = account_receivable.convert_to<std::string> ();

				if (include_confirmed)
				{
					auto account_receivable = node.ledger.account_receivable (transaction, account, true);
					response_l["confirmed_pending"] = account_receivable.convert_to<std::string> ();
					response_l["confirmed_receivable"] = account_receivable.convert_to<std::string> ();
				}
			}
		}
	}
	response_errors ();
}

void nano::json_handler::account_key ()
{
	auto account (account_impl ());
	if (!ec)
	{
		response_l["key"] = account.to_string ();
	}
	response_errors ();
}

void nano::json_handler::account_list ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		boost::json::array accounts;
		for (auto const & account : wallet->accounts ())
		{
			accounts.push_back (boost::json::value (account.to_account ()));
		}
		response_l["accounts"] = std::move (accounts);
	}
	response_errors ();
}

void nano::json_handler::account_move ()
{
	node.workers.post (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		auto wallet (rpc_l->wallet_impl ());
		if (!rpc_l->ec)
		{
			std::string source_text (rpc_l->request.at ("source").as_string ().c_str ());
			auto const & accounts_arr = rpc_l->request.at ("accounts").as_array ();
			nano::wallet_id source;
			if (!source.decode_hex (source_text))
			{
				auto existing (rpc_l->node.wallets.items.find (source));
				if (existing != rpc_l->node.wallets.items.end ())
				{
					auto source_wallet (existing->second);
					std::vector<nano::public_key> accounts;
					for (auto const & acc_val : accounts_arr)
					{
						auto account (rpc_l->account_impl (acc_val.as_string ().c_str ()));
						accounts.push_back (account);
					}
					auto error (wallet->move_accounts (*source_wallet, accounts));
					rpc_l->response_l["moved"] = error ? "0" : "1";
				}
				else
				{
					rpc_l->ec = nano::error_rpc::source_not_found;
				}
			}
			else
			{
				rpc_l->ec = nano::error_rpc::bad_source;
			}
		}
		rpc_l->response_errors ();
	}));
}

void nano::json_handler::account_remove ()
{
	node.workers.post (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		auto wallet (rpc_l->wallet_impl ());
		auto account (rpc_l->account_impl ());
		if (!rpc_l->ec)
		{
			rpc_l->wallet_locked_impl (wallet);
			rpc_l->wallet_account_impl (wallet, account);
			if (!rpc_l->ec)
			{
				wallet->remove_account (account);
				rpc_l->response_l["removed"] = "1";
			}
		}
		rpc_l->response_errors ();
	}));
}

void nano::json_handler::account_representative ()
{
	auto account (account_impl ());
	if (!ec)
	{
		auto transaction = node.ledger.tx_begin_read ();
		auto info (account_info_impl (transaction, account));
		if (!ec)
		{
			response_l["representative"] = info.representative.to_account ();
		}
	}
	response_errors ();
}

void nano::json_handler::account_representative_set ()
{
	node.workers.post (create_worker_task ([work_generation_enabled = node.work_generation_enabled ()] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		auto wallet (rpc_l->wallet_impl ());
		auto account (rpc_l->account_impl ());
		std::string representative_text (rpc_l->request.at ("representative").as_string ().c_str ());
		auto representative (rpc_l->account_impl (representative_text, nano::error_rpc::bad_representative_number));
		if (!rpc_l->ec)
		{
			auto work (rpc_l->work_optional_impl ());
			if (!rpc_l->ec && work)
			{
				rpc_l->wallet_locked_impl (wallet);
				rpc_l->wallet_account_impl (wallet, account);
				if (!rpc_l->ec)
				{
					auto block_transaction = rpc_l->node.ledger.tx_begin_read ();
					auto info (rpc_l->account_info_impl (block_transaction, account));
					if (!rpc_l->ec)
					{
						nano::block_details details (info.epoch (), false, false, false);
						if (rpc_l->node.network_params.work.difficulty (nano::work_version::work_1, info.head, work) < rpc_l->node.network_params.work.threshold (nano::work_version::work_1, details))
						{
							rpc_l->ec = nano::error_common::invalid_work;
						}
					}
				}
			}
			else if (!rpc_l->ec) // work == 0
			{
				if (!work_generation_enabled)
				{
					rpc_l->ec = nano::error_common::disabled_work_generation;
				}
			}
			if (!rpc_l->ec)
			{
				bool generate_work (work == 0); // Disable work generation if "work" option is provided
				auto response_a (rpc_l->response);
				auto response_data (std::make_shared<boost::json::object> (rpc_l->response_l));
				wallet->change_async (
				account, representative, [response_a, response_data] (std::shared_ptr<nano::block> const & block) {
					if (block != nullptr)
					{
						(*response_data)["block"] = block->hash ().to_string ();
						response_a (boost::json::serialize (*response_data));
					}
					else
					{
						json_error_response (response_a, "Error generating block");
					}
				},
				work, generate_work);
			}
		}
		// Because of change_async
		if (rpc_l->ec)
		{
			rpc_l->response_errors ();
		}
	}));
}

void nano::json_handler::account_weight ()
{
	auto account (account_impl ());
	if (!ec)
	{
		auto balance (node.weight (account));
		response_l["weight"] = balance.convert_to<std::string> ();
	}
	response_errors ();
}

void nano::json_handler::accounts_balances ()
{
	boost::json::object balances;
	boost::json::object errors;
	auto transaction = node.store.tx_begin_read ();
	for (auto const & account_from_request : request.at ("accounts").as_array ())
	{
		boost::json::object entry;
		std::string account_text (account_from_request.as_string ().c_str ());
		auto account = account_impl (account_text);
		if (!ec)
		{
			bool const include_only_confirmed = request.contains ("include_only_confirmed") ? request.at ("include_only_confirmed").as_bool () : true;
			auto balance = node.balance_pending (account, include_only_confirmed);
			entry["balance"] = balance.first.convert_to<std::string> ();
			entry["pending"] = balance.second.convert_to<std::string> ();
			entry["receivable"] = balance.second.convert_to<std::string> ();
			balances[account_text] = std::move (entry);
			continue;
		}
		debug_assert (ec);
		errors[account_text] = ec.message ();
		ec = {};
	}
	if (!balances.empty ())
	{
		response_l["balances"] = std::move (balances);
	}
	if (!errors.empty ())
	{
		response_l["errors"] = std::move (errors);
	}
	response_errors ();
}

void nano::json_handler::accounts_representatives ()
{
	boost::json::object representatives;
	boost::json::object errors;
	auto transaction = node.ledger.tx_begin_read ();
	for (auto const & account_from_request : request.at ("accounts").as_array ())
	{
		std::string account_text (account_from_request.as_string ().c_str ());
		auto account = account_impl (account_text);
		if (!ec)
		{
			auto info = account_info_impl (transaction, account);
			if (!ec)
			{
				representatives[account_text] = info.representative.to_account ();
				continue;
			}
		}
		debug_assert (ec);
		errors[account_text] = ec.message ();
		ec = {};
	}
	if (!representatives.empty ())
	{
		response_l["representatives"] = std::move (representatives);
	}
	if (!errors.empty ())
	{
		response_l["errors"] = std::move (errors);
	}
	response_errors ();
}

void nano::json_handler::accounts_create ()
{
	node.workers.post (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		auto wallet (rpc_l->wallet_impl ());
		auto count (rpc_l->count_impl ());
		if (!rpc_l->ec)
		{
			bool const generate_work = rpc_l->request.contains ("work") ? rpc_l->request.at ("work").as_bool () : false;
			boost::json::array accounts;
			for (auto i (0); accounts.size () < count; ++i)
			{
				nano::account new_key (wallet->deterministic_insert (generate_work));
				if (!new_key.is_zero ())
				{
					accounts.push_back (boost::json::value (new_key.to_account ()));
				}
			}
			rpc_l->response_l["accounts"] = std::move (accounts);
		}
		rpc_l->response_errors ();
	}));
}

void nano::json_handler::accounts_frontiers ()
{
	boost::json::object frontiers;
	boost::json::object errors;
	auto transaction = node.ledger.tx_begin_read ();
	for (auto const & account_from_request : request.at ("accounts").as_array ())
	{
		std::string account_text (account_from_request.as_string ().c_str ());
		auto account = account_impl (account_text);
		if (!ec)
		{
			auto latest = node.ledger.any.account_head (transaction, account);
			if (!latest.is_zero ())
			{
				frontiers[account.to_account ()] = latest.to_string ();
				continue;
			}
			else
			{
				ec = nano::error_common::account_not_found;
			}
		}
		debug_assert (ec);
		errors[account_text] = ec.message ();
		ec = {};
	}
	if (!frontiers.empty ())
	{
		response_l["frontiers"] = std::move (frontiers);
	}
	if (!errors.empty ())
	{
		response_l["errors"] = std::move (errors);
	}
	response_errors ();
}

void nano::json_handler::accounts_pending ()
{
	response_l["deprecated"] = "1";
	accounts_receivable ();
}

void nano::json_handler::accounts_receivable ()
{
	auto count (count_optional_impl ());
	auto threshold (threshold_optional_impl ());
	bool const source = request.contains ("source") ? request.at ("source").as_bool () : false;
	bool const include_active = request.contains ("include_active") ? request.at ("include_active").as_bool () : false;
	bool const include_only_confirmed = request.contains ("include_only_confirmed") ? request.at ("include_only_confirmed").as_bool () : true;
	bool const sorting = request.contains ("sorting") ? request.at ("sorting").as_bool () : false;
	auto simple (threshold.is_zero () && !source && !sorting); // if simple, response is a list of hashes for each account
	boost::json::object pending;
	auto transaction = node.ledger.tx_begin_read ();
	for (auto const & acc_val : request.at ("accounts").as_array ())
	{
		std::string account_text (acc_val.as_string ().c_str ());
		auto account (account_impl (account_text));
		if (!ec)
		{
			boost::json::array simple_peers_l;
			boost::json::object peers_l;
			std::size_t peers_count = 0;
			for (auto i (node.store.pending.begin (transaction, nano::pending_key (account, 0))), n (node.store.pending.end (transaction)); i != n && nano::pending_key (i->first).account == account && peers_count < count; ++i)
			{
				nano::pending_key const & key (i->first);
				if (block_confirmed (node, transaction, key.hash, include_active, include_only_confirmed))
				{
					if (simple)
					{
						simple_peers_l.push_back (boost::json::value (key.hash.to_string ()));
						++peers_count;
					}
					else
					{
						nano::pending_info const & info (i->second);
						if (info.amount.number () >= threshold.number ())
						{
							if (source)
							{
								boost::json::object pending_tree;
								pending_tree["amount"] = info.amount.number ().convert_to<std::string> ();
								pending_tree["source"] = info.source.to_account ();
								peers_l[key.hash.to_string ()] = std::move (pending_tree);
							}
							else
							{
								peers_l[key.hash.to_string ()] = info.amount.number ().convert_to<std::string> ();
							}
							++peers_count;
						}
					}
				}
			}
			// Note: sorting is not supported with boost::json objects directly
			// The original ptree sorting is removed as it doesn't translate directly
			if (simple)
			{
				if (!simple_peers_l.empty ())
				{
					pending[account.to_account ()] = std::move (simple_peers_l);
				}
			}
			else
			{
				if (!peers_l.empty ())
				{
					pending[account.to_account ()] = std::move (peers_l);
				}
			}
		}
	}
	response_l["blocks"] = std::move (pending);
	response_errors ();
}

void nano::json_handler::active_difficulty ()
{
	auto include_trend (request.contains ("include_trend") ? request.at ("include_trend").as_bool () : false);
	auto const multiplier_active = 1.0;
	auto const default_difficulty (node.default_difficulty (nano::work_version::work_1));
	auto const default_receive_difficulty (node.default_receive_difficulty (nano::work_version::work_1));
	auto const receive_current_denormalized (node.network_params.work.denormalized_multiplier (multiplier_active, node.network_params.work.epoch_2_receive));
	response_l["deprecated"] = "1";
	response_l["network_minimum"] = nano::to_string_hex (default_difficulty);
	response_l["network_receive_minimum"] = nano::to_string_hex (default_receive_difficulty);
	response_l["network_current"] = nano::to_string_hex (nano::difficulty::from_multiplier (multiplier_active, default_difficulty));
	response_l["network_receive_current"] = nano::to_string_hex (nano::difficulty::from_multiplier (receive_current_denormalized, default_receive_difficulty));
	response_l["multiplier"] = 1.0;
	if (include_trend)
	{
		boost::json::array difficulty_trend_l;
		difficulty_trend_l.push_back (boost::json::value ("1.000000000000000"));
		response_l["difficulty_trend"] = std::move (difficulty_trend_l);
	}
	response_errors ();
}

void nano::json_handler::available_supply ()
{
	auto genesis_balance (node.balance (node.network_params.ledger.genesis->account ())); // Cold storage genesis
	auto landing_balance (node.balance (nano::account ("059F68AAB29DE0D3A27443625C7EA9CDDB6517A8B76FE37727EF6A4D76832AD5"))); // Active unavailable account
	auto faucet_balance (node.balance (nano::account ("8E319CE6F3025E5B2DF66DA7AB1467FE48F1679C13DD43BFDB29FA2E9FC40D3B"))); // Faucet account
	auto burned_balance ((node.balance_pending (nano::account{}, false)).second); // Burning 0 account
	auto available (nano::dev::constants.genesis_amount - genesis_balance - landing_balance - faucet_balance - burned_balance);
	response_l["available"] = available.convert_to<std::string> ();
	response_errors ();
}

void nano::json_handler::block_info ()
{
	auto hash (hash_impl ());
	if (!ec)
	{
		auto transaction = node.ledger.tx_begin_read ();
		auto block = node.ledger.any.block_get (transaction, hash);
		if (block != nullptr)
		{
			auto account = block->account ();
			response_l["block_account"] = account.to_account ();
			bool include_linked_account = request.contains ("include_linked_account") ? request.at ("include_linked_account").as_bool () : false;
			if (include_linked_account)
			{
				auto linked_account = node.ledger.linked_account (transaction, *block);
				if (linked_account.has_value ())
				{
					response_l["linked_account"] = linked_account.value ().to_account ();
				}
				else
				{
					response_l["linked_account"] = "0";
				}
			}
			auto amount = node.ledger.any.block_amount (transaction, hash);
			if (amount)
			{
				response_l["amount"] = amount.value ().number ().convert_to<std::string> ();
			}
			auto balance = block->balance ();
			response_l["balance"] = balance.number ().convert_to<std::string> ();
			response_l["height"] = std::to_string (block->sideband ().height);
			response_l["local_timestamp"] = std::to_string (block->sideband ().timestamp);
			response_l["successor"] = block->sideband ().successor.to_string ();
			auto confirmed (node.ledger.confirmed.block_exists_or_pruned (transaction, hash));
			response_l["confirmed"] = confirmed;

			bool json_block_l = request.contains ("json_block") ? request.at ("json_block").as_bool () : false;
			if (json_block_l)
			{
				boost::json::object block_node_l;
				block->serialize_json (block_node_l);
				response_l["contents"] = std::move (block_node_l);
			}
			else
			{
				std::string contents;
				block->serialize_json (contents);
				response_l["contents"] = contents;
			}
			if (block->type () == nano::block_type::state)
			{
				auto subtype (nano::state_subtype (block->sideband ().details));
				response_l["subtype"] = subtype;
			}
		}
		else
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void nano::json_handler::block_confirm ()
{
	auto hash (hash_impl ());
	if (!ec)
	{
		auto transaction = node.ledger.tx_begin_read ();
		auto block_l = node.ledger.any.block_get (transaction, hash);
		if (block_l != nullptr)
		{
			if (!node.ledger.confirmed.block_exists_or_pruned (transaction, hash))
			{
				// Start new confirmation for unconfirmed (or not being confirmed) block
				if (!node.cementing_set.contains (hash))
				{
					node.start_election (std::move (block_l));
				}
			}
			else
			{
				// Add record in confirmation history for confirmed block
				nano::election_status status{ block_l, nano::election_status_type::active_confirmation_height };
				node.active.recently_cemented.put (status);
				// Trigger callback for confirmed block
				auto account = block_l->account ();
				auto amount = node.ledger.any.block_amount (transaction, hash);
				bool is_state_send (false);
				bool is_state_epoch (false);
				if (amount)
				{
					if (auto state = dynamic_cast<nano::state_block *> (block_l.get ()))
					{
						is_state_send = state->is_send ();
						is_state_epoch = amount.value () == 0 && node.ledger.is_epoch_link (state->link_field ().value ());
					}
				}
				node.observers.blocks.notify (status, {}, account, amount ? amount.value ().number () : 0, is_state_send, is_state_epoch);
			}
			response_l["started"] = "1";
		}
		else
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void nano::json_handler::blocks ()
{
	bool const json_block_l = request.contains ("json_block") ? request.at ("json_block").as_bool () : false;
	boost::json::object blocks;
	auto transaction = node.ledger.tx_begin_read ();
	for (auto const & hash_val : request.at ("hashes").as_array ())
	{
		if (!ec)
		{
			std::string hash_text (hash_val.as_string ().c_str ());
			nano::block_hash hash;
			if (!hash.decode_hex (hash_text))
			{
				auto block = node.ledger.any.block_get (transaction, hash);
				if (block != nullptr)
				{
					if (json_block_l)
					{
						boost::json::object block_node_l;
						block->serialize_json (block_node_l);
						blocks[hash_text] = std::move (block_node_l);
					}
					else
					{
						std::string contents;
						block->serialize_json (contents);
						blocks[hash_text] = contents;
					}
				}
				else
				{
					ec = nano::error_blocks::not_found;
				}
			}
			else
			{
				ec = nano::error_blocks::bad_hash_number;
			}
		}
	}
	response_l["blocks"] = std::move (blocks);
	response_errors ();
}

void nano::json_handler::blocks_info ()
{
	bool const pending = request.contains ("pending") ? request.at ("pending").as_bool () : false;
	bool const receivable = request.contains ("receivable") ? request.at ("receivable").as_bool () : pending;
	bool const receive_hash = request.contains ("receive_hash") ? request.at ("receive_hash").as_bool () : false;
	bool const source = request.contains ("source") ? request.at ("source").as_bool () : false;
	bool const json_block_l = request.contains ("json_block") ? request.at ("json_block").as_bool () : false;
	bool const include_linked_account = request.contains ("include_linked_account") ? request.at ("include_linked_account").as_bool () : false;
	bool const include_not_found = request.contains ("include_not_found") ? request.at ("include_not_found").as_bool () : false;

	boost::json::object blocks;
	boost::json::array blocks_not_found;
	auto transaction = node.ledger.tx_begin_read ();
	for (auto const & hash_val : request.at ("hashes").as_array ())
	{
		if (!ec)
		{
			std::string hash_text (hash_val.as_string ().c_str ());
			nano::block_hash hash;
			if (!hash.decode_hex (hash_text))
			{
				auto block = node.ledger.any.block_get (transaction, hash);
				if (block != nullptr)
				{
					boost::json::object entry;
					auto account = block->account ();
					entry["block_account"] = account.to_account ();
					if (include_linked_account)
					{
						auto linked_account = node.ledger.linked_account (transaction, *block);
						if (linked_account.has_value ())
						{
							entry["linked_account"] = linked_account.value ().to_account ();
						}
						else
						{
							entry["linked_account"] = "0";
						}
					}
					auto amount = node.ledger.any.block_amount (transaction, hash);
					if (amount)
					{
						entry["amount"] = amount.value ().number ().convert_to<std::string> ();
					}
					auto balance = block->balance ();
					entry["balance"] = balance.number ().convert_to<std::string> ();
					entry["height"] = std::to_string (block->sideband ().height);
					entry["local_timestamp"] = std::to_string (block->sideband ().timestamp);
					entry["successor"] = block->sideband ().successor.to_string ();
					auto confirmed (node.ledger.confirmed.block_exists_or_pruned (transaction, hash));
					entry["confirmed"] = confirmed;

					if (json_block_l)
					{
						boost::json::object block_node_l;
						block->serialize_json (block_node_l);
						entry["contents"] = std::move (block_node_l);
					}
					else
					{
						std::string contents;
						block->serialize_json (contents);
						entry["contents"] = contents;
					}
					if (block->type () == nano::block_type::state)
					{
						auto subtype (nano::state_subtype (block->sideband ().details));
						entry["subtype"] = subtype;
					}
					if (receivable || receive_hash)
					{
						if (!block->is_send ())
						{
							if (receivable)
							{
								entry["pending"] = "0";
								entry["receivable"] = "0";
							}
							if (receive_hash)
							{
								entry["receive_hash"] = nano::block_hash (0).to_string ();
							}
						}
						else if (node.ledger.any.pending_get (transaction, nano::pending_key{ block->destination (), hash }))
						{
							if (receivable)
							{
								entry["pending"] = "1";
								entry["receivable"] = "1";
							}
							if (receive_hash)
							{
								entry["receive_hash"] = nano::block_hash (0).to_string ();
							}
						}
						else
						{
							if (receivable)
							{
								entry["pending"] = "0";
								entry["receivable"] = "0";
							}
							if (receive_hash)
							{
								std::shared_ptr<nano::block> receive_block = node.ledger.find_receive_block_by_send_hash (transaction, block->destination (), hash);
								std::string receive_hash = receive_block ? receive_block->hash ().to_string () : nano::block_hash (0).to_string ();
								entry["receive_hash"] = receive_hash;
							}
						}
					}
					if (source)
					{
						if (!block->is_receive () || !node.ledger.any.block_exists (transaction, block->source ()))
						{
							entry["source_account"] = "0";
						}
						else
						{
							auto block_a = node.ledger.any.block_get (transaction, block->source ());
							release_assert (block_a);
							entry["source_account"] = block_a->account ().to_account ();
						}
					}
					blocks[hash_text] = std::move (entry);
				}
				else if (include_not_found)
				{
					blocks_not_found.push_back (boost::json::value (hash_text));
				}
				else
				{
					ec = nano::error_blocks::not_found;
				}
			}
			else
			{
				ec = nano::error_blocks::bad_hash_number;
			}
		}
	}
	if (!ec)
	{
		response_l["blocks"] = std::move (blocks);
		if (include_not_found)
		{
			response_l["blocks_not_found"] = std::move (blocks_not_found);
		}
	}
	response_errors ();
}

void nano::json_handler::block_account ()
{
	auto hash (hash_impl ());
	if (!ec)
	{
		auto transaction = node.ledger.tx_begin_read ();
		auto block = node.ledger.any.block_get (transaction, hash);
		if (block)
		{
			response_l["account"] = block->account ().to_account ();
		}
		else
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void nano::json_handler::block_count ()
{
	response_l["count"] = std::to_string (node.ledger.block_count ());
	response_l["unchecked"] = std::to_string (node.unchecked.count ());
	response_l["cemented"] = std::to_string (node.ledger.cemented_count ());
	if (node.flags.enable_pruning)
	{
		response_l["full"] = std::to_string (node.ledger.block_count () - node.ledger.pruned_count ());
		response_l["pruned"] = std::to_string (node.ledger.pruned_count ());
	}
	response_errors ();
}

void nano::json_handler::block_create ()
{
	std::string type (request.at ("type").as_string ().c_str ());
	nano::wallet_id wallet (0);
	// Default to work_1 if not specified
	auto work_version (work_version_optional_impl (nano::work_version::work_1));
	auto difficulty_l (difficulty_optional_impl (work_version));
	if (auto* wallet_val = request.if_contains ("wallet"))
	{
		std::string wallet_text (wallet_val->as_string ().c_str ());
		if (!ec && wallet.decode_hex (wallet_text))
		{
			ec = nano::error_common::bad_wallet_number;
		}
	}
	nano::account account{};
	if (auto* account_val = request.if_contains ("account"))
	{
		std::string account_text (account_val->as_string ().c_str ());
		if (!ec)
		{
			account = account_impl (account_text);
		}
	}
	nano::account representative{};
	if (auto* representative_val = request.if_contains ("representative"))
	{
		std::string representative_text (representative_val->as_string ().c_str ());
		if (!ec)
		{
			representative = account_impl (representative_text, nano::error_rpc::bad_representative_number);
		}
	}
	nano::account destination{};
	if (auto* destination_val = request.if_contains ("destination"))
	{
		std::string destination_text (destination_val->as_string ().c_str ());
		if (!ec)
		{
			destination = account_impl (destination_text, nano::error_rpc::bad_destination);
		}
	}
	nano::block_hash source (0);
	if (auto* source_val = request.if_contains ("source"))
	{
		std::string source_text (source_val->as_string ().c_str ());
		if (!ec && source.decode_hex (source_text))
		{
			ec = nano::error_rpc::bad_source;
		}
	}
	nano::amount amount (0);
	if (auto* amount_val = request.if_contains ("amount"))
	{
		std::string amount_text (amount_val->as_string ().c_str ());
		if (!ec && amount.decode_dec (amount_text))
		{
			ec = nano::error_common::invalid_amount;
		}
	}
	auto work (work_optional_impl ());
	nano::raw_key prv;
	prv.clear ();
	nano::block_hash previous (0);
	nano::amount balance (0);
	if (work == 0 && !node.work_generation_enabled ())
	{
		ec = nano::error_common::disabled_work_generation;
	}
	if (!ec && wallet != 0 && account != 0)
	{
		auto existing (node.wallets.items.find (wallet));
		if (existing != node.wallets.items.end ())
		{
			wallet_locked_impl (existing->second);
			wallet_account_impl (existing->second, account);
			if (!ec)
			{
				existing->second->fetch_prv (account, prv);
				auto block_transaction = node.ledger.tx_begin_read ();
				previous = node.ledger.any.account_head (block_transaction, account);
				balance = node.ledger.any.account_balance (block_transaction, account).value_or (0);
			}
		}
		else
		{
			ec = nano::error_common::wallet_not_found;
		}
	}
	bool key_provided = false;
	if (auto* key_val = request.if_contains ("key"))
	{
		key_provided = true;
		std::string key_text (key_val->as_string ().c_str ());
		if (!ec && prv.decode_hex (key_text))
		{
			ec = nano::error_common::bad_private_key;
		}
	}
	bool previous_provided = false;
	if (auto* previous_val = request.if_contains ("previous"))
	{
		previous_provided = true;
		std::string previous_text (previous_val->as_string ().c_str ());
		if (!ec && previous.decode_hex (previous_text))
		{
			ec = nano::error_rpc::bad_previous;
		}
	}
	bool balance_provided = false;
	if (auto* balance_val = request.if_contains ("balance"))
	{
		balance_provided = true;
		std::string balance_text (balance_val->as_string ().c_str ());
		if (!ec && balance.decode_dec (balance_text))
		{
			ec = nano::error_rpc::invalid_balance;
		}
	}
	nano::link link (0);
	bool link_provided = false;
	if (auto* link_val = request.if_contains ("link"))
	{
		link_provided = true;
		std::string link_text (link_val->as_string ().c_str ());
		if (!ec)
		{
			if (link.decode_account (link_text))
			{
				if (link.decode_hex (link_text))
				{
					ec = nano::error_rpc::bad_link;
				}
			}
		}
	}
	else
	{
		// Retrieve link from source or destination
		if (source.is_zero ())
		{
			link = destination;
		}
		else
		{
			link = source;
		}
	}
	if (!ec)
	{
		auto rpc_l (shared_from_this ());
		// Serializes the block contents to the RPC response
		auto block_response_put_l = [rpc_l, this] (nano::block const & block_a) {
			boost::json::object response_l;
			response_l["hash"] = block_a.hash ().to_string ();
			response_l["difficulty"] = nano::to_string_hex (rpc_l->node.network_params.work.difficulty (block_a));
			bool json_block_l = request.contains ("json_block") ? request.at ("json_block").as_bool () : false;
			if (json_block_l)
			{
				boost::json::object block_node_l;
				block_a.serialize_json (block_node_l);
				response_l["block"] = std::move (block_node_l);
			}
			else
			{
				std::string contents;
				block_a.serialize_json (contents);
				response_l["block"] = contents;
			}
			rpc_l->response (boost::json::serialize (response_l));
		};
		// Wrapper from argument to lambda capture, to extend the block's scope
		auto get_callback_l = [rpc_l, block_response_put_l] (std::shared_ptr<nano::block> const & block_a) {
			// Callback upon work generation success or failure
			return [block_a, rpc_l, block_response_put_l] (std::optional<uint64_t> const & work_a) {
				if (block_a != nullptr)
				{
					if (work_a.has_value ())
					{
						block_a->block_work_set (*work_a);
						block_response_put_l (*block_a);
					}
					else
					{
						rpc_l->ec = nano::error_common::failure_work_generation;
					}
				}
				else
				{
					rpc_l->ec = nano::error_common::generic;
				}
				if (rpc_l->ec)
				{
					rpc_l->response_errors ();
				}
			};
		};
		if (prv != 0)
		{
			nano::account pub (nano::pub_key (prv));
			// Fetching account balance & previous for send blocks (if aren't given directly)
			if (!previous_provided && !balance_provided)
			{
				auto transaction = node.ledger.tx_begin_read ();
				previous = node.ledger.any.account_head (transaction, pub);
				balance = node.ledger.any.account_balance (transaction, pub).value_or (0);
			}
			// Double check current balance if previous block is specified
			else if (previous_provided && balance_provided && type == "send")
			{
				auto transaction = node.ledger.tx_begin_read ();
				if (node.ledger.any.block_exists (transaction, previous) && node.ledger.any.block_balance (transaction, previous) != balance.number ())
				{
					ec = nano::error_rpc::block_create_balance_mismatch;
				}
			}
			// Check for incorrect account key
			if (!ec && request.contains ("account"))
			{
				if (account != pub)
				{
					ec = nano::error_rpc::block_create_public_key_mismatch;
				}
			}
			nano::block_builder builder_l;
			std::shared_ptr<nano::block> block_l{ nullptr };
			nano::root root_l;
			std::error_code ec_build;
			if (type == "state")
			{
				if (previous_provided && !representative.is_zero () && (!link.is_zero () || link_provided))
				{
					block_l = builder_l.state ()
							  .account (pub)
							  .previous (previous)
							  .representative (representative)
							  .balance (balance)
							  .link (link)
							  .sign (prv, pub)
							  .build (ec_build);
					if (previous.is_zero ())
					{
						root_l = pub;
					}
					else
					{
						root_l = previous;
					}
				}
				else
				{
					ec = nano::error_rpc::block_create_requirements_state;
				}
			}
			else if (type == "open")
			{
				if (representative != 0 && source != 0)
				{
					block_l = builder_l.open ()
							  .account (pub)
							  .source (source)
							  .representative (representative)
							  .sign (prv, pub)
							  .build (ec_build);
					root_l = pub;
				}
				else
				{
					ec = nano::error_rpc::block_create_requirements_open;
				}
			}
			else if (type == "receive")
			{
				if (source != 0 && previous != 0)
				{
					block_l = builder_l.receive ()
							  .previous (previous)
							  .source (source)
							  .sign (prv, pub)
							  .build (ec_build);
					root_l = previous;
				}
				else
				{
					ec = nano::error_rpc::block_create_requirements_receive;
				}
			}
			else if (type == "change")
			{
				if (representative != 0 && previous != 0)
				{
					block_l = builder_l.change ()
							  .previous (previous)
							  .representative (representative)
							  .sign (prv, pub)
							  .build (ec_build);
					root_l = previous;
				}
				else
				{
					ec = nano::error_rpc::block_create_requirements_change;
				}
			}
			else if (type == "send")
			{
				if (destination != 0 && previous != 0 && balance != 0 && amount != 0)
				{
					if (balance.number () >= amount.number ())
					{
						block_l = builder_l.send ()
								  .previous (previous)
								  .destination (destination)
								  .balance (balance.number () - amount.number ())
								  .sign (prv, pub)
								  .build (ec_build);
						root_l = previous;
					}
					else
					{
						ec = nano::error_common::insufficient_balance;
					}
				}
				else
				{
					ec = nano::error_rpc::block_create_requirements_send;
				}
			}
			else
			{
				ec = nano::error_blocks::invalid_type;
			}
			if (!ec && (!ec_build || ec_build == nano::error_common::missing_work))
			{
				if (work == 0)
				{
					// Difficulty calculation
					if (!request.contains ("difficulty"))
					{
						difficulty_l = difficulty_ledger (*block_l);
					}
					node.work_generate (work_version, root_l, difficulty_l, get_callback_l (block_l), nano::account (pub));
				}
				else
				{
					block_l->block_work_set (work);
					block_response_put_l (*block_l);
				}
			}
		}
		else
		{
			ec = nano::error_rpc::block_create_key_required;
		}
	}
	// Because of callback
	if (ec)
	{
		response_errors ();
	}
}

void nano::json_handler::block_hash ()
{
	auto block (block_impl (true));

	if (!ec)
	{
		response_l["hash"] = block->hash ().to_string ();
	}
	response_errors ();
}

void nano::json_handler::bootstrap ()
{
	std::string address_text (request.at ("address").as_string ().c_str ());
	std::string port_text (request.at ("port").as_string ().c_str ());
	boost::system::error_code address_ec;
	auto address (boost::asio::ip::make_address_v6 (address_text, address_ec));
	if (!address_ec)
	{
		uint16_t port;
		if (!nano::parse_port (port_text, port))
		{
			ec = nano::error_rpc::disabled_bootstrap_legacy;
		}
		else
		{
			ec = nano::error_common::invalid_port;
		}
	}
	else
	{
		ec = nano::error_common::invalid_ip_address;
	}
	response_errors ();
}

void nano::json_handler::bootstrap_any ()
{
	bool const force = request.contains ("force") ? request.at ("force").as_bool () : false;
	ec = nano::error_rpc::disabled_bootstrap_legacy;
	response_errors ();
}

void nano::json_handler::bootstrap_lazy ()
{
	auto hash (hash_impl ());
	bool const force = request.contains ("force") ? request.at ("force").as_bool () : false;
	ec = nano::error_rpc::disabled_bootstrap_lazy;
	response_errors ();
}

/*
 * @warning This is an internal/diagnostic RPC, do not rely on its interface being stable
 */
void nano::json_handler::bootstrap_status ()
{
	auto status = node.bootstrap.status ();

	// Only summary information
	response_l["priorities"] = status.priorities;
	response_l["blocking"] = status.blocking;

	response_errors ();
}

/*
 * @warning This is an internal/diagnostic RPC, do not rely on its interface being stable
 */
void nano::json_handler::bootstrap_priorities ()
{
	if (!ec)
	{
		auto [blocking, priorities] = node.bootstrap.info ();

		// Priorities
		{
			boost::json::array resp_l;
			for (auto const & entry : priorities)
			{
				boost::json::object entry_l;
				entry_l["account"] = entry.account.to_account ();
				entry_l["priority"] = entry.priority;

				resp_l.push_back (std::move (entry_l));
			}
			response_l["priorities"] = std::move (resp_l);
		}
		// Blocking
		{
			boost::json::array resp_l;
			for (auto const & entry : blocking)
			{
				boost::json::object entry_l;
				entry_l["account"] = entry.account.to_account ();
				entry_l["dependency"] = entry.dependency.to_string ();
				entry_l["dependency_account"] = entry.dependency_account.to_account ();

				resp_l.push_back (std::move (entry_l));
			}
			response_l["blocking"] = std::move (resp_l);
		}
	}
	response_errors ();
}

/*
 * @warning This is an internal/diagnostic RPC, do not rely on its interface being stable
 */
void nano::json_handler::bootstrap_reset ()
{
	node.bootstrap.reset ();
	response_l["success"] = "";
	response_errors ();
}

void nano::json_handler::chain (bool successors)
{
	successors = successors != (request.contains ("reverse") ? request.at ("reverse").as_bool () : false);
	auto hash (hash_impl ("block"));
	auto count (count_impl ());
	auto offset (offset_optional_impl (0));
	if (!ec)
	{
		boost::json::array blocks;
		auto transaction = node.ledger.tx_begin_read ();
		while (!hash.is_zero () && blocks.size () < count)
		{
			auto block_l = node.ledger.any.block_get (transaction, hash);
			if (block_l != nullptr)
			{
				if (offset > 0)
				{
					--offset;
				}
				else
				{
					blocks.push_back (boost::json::value (hash.to_string ()));
				}
				hash = successors ? node.ledger.any.block_successor (transaction, hash).value_or (0) : block_l->previous ();
			}
			else
			{
				hash.clear ();
			}
		}
		response_l["blocks"] = std::move (blocks);
	}
	response_errors ();
}

void nano::json_handler::confirmation_active ()
{
	uint64_t announcements (0);
	uint64_t confirmed (0);
	if (auto* announcements_val = request.if_contains ("announcements"))
	{
		announcements = strtoul (announcements_val->as_string ().c_str (), NULL, 10);
	}
	boost::json::array elections;
	auto active_elections = node.active.list_active ();
	for (auto const & election : active_elections)
	{
		if (election->confirmation_request_count >= announcements)
		{
			if (!election->confirmed ())
			{
				elections.push_back (boost::json::value (election->qualified_root.to_string ()));
			}
			else
			{
				++confirmed;
			}
		}
	}
	auto elections_size = elections.size ();
	response_l["confirmations"] = std::move (elections);
	response_l["unconfirmed"] = static_cast<std::uint64_t> (elections_size);
	response_l["confirmed"] = confirmed;
	response_errors ();
}

void nano::json_handler::election_statistics ()
{
	auto active_elections = node.active.list_active ();
	unsigned manual_count = 0;
	unsigned priority_count = 0;
	unsigned hinted_count = 0;
	unsigned optimistic_count = 0;
	unsigned total_count = 0;
	std::chrono::steady_clock::duration total_age{};
	auto now = std::chrono::steady_clock::now ();
	std::chrono::steady_clock::time_point oldest_election_start = now;

	for (auto const & election : active_elections)
	{
		total_count++;
		auto election_start = election->get_election_start ();
		auto age = now - election_start;
		total_age += age;
		oldest_election_start = std::min (oldest_election_start, election->get_election_start ());

		switch (election->behavior ())
		{
			case election_behavior::manual:
				manual_count++;
				break;
			case election_behavior::priority:
				priority_count++;
				break;
			case election_behavior::hinted:
				hinted_count++;
				break;
			case election_behavior::optimistic:
				optimistic_count++;
				break;
		}
	}

	auto utilization_percentage = (static_cast<double> (total_count * 100) / node.config.active_elections.size);
	auto max_election_age = std::chrono::duration_cast<std::chrono::milliseconds> (now - oldest_election_start).count ();
	auto average_election_age = total_count ? std::chrono::duration_cast<std::chrono::milliseconds> (total_age).count () / total_count : 0;

	std::stringstream stream_utilization;
	stream_utilization << std::fixed << std::setprecision (2) << utilization_percentage;

	response_l["manual"] = manual_count;
	response_l["priority"] = priority_count;
	response_l["hinted"] = hinted_count;
	response_l["optimistic"] = optimistic_count;
	response_l["total"] = total_count;
	response_l["aec_utilization_percentage"] = stream_utilization.str ();
	response_l["max_election_age"] = max_election_age;
	response_l["average_election_age"] = average_election_age;

	response_errors ();
}

void nano::json_handler::confirmation_history ()
{
	boost::json::array elections;
	boost::json::object confirmation_stats;
	std::chrono::milliseconds running_total (0);
	nano::block_hash hash (0);
	if (request.contains ("hash"))
	{
		hash = hash_impl ();
	}
	if (!ec)
	{
		// TODO: Allow passing a count parameter to limit the number of results
		// Default to 2000 for now since it was the previous limit
		for (auto const & status : node.active.recently_cemented.list (2000))
		{
			if (hash.is_zero () || status.winner->hash () == hash)
			{
				boost::json::object election;
				election["hash"] = status.winner->hash ().to_string ();
				election["duration"] = status.election_duration.count ();
				election["time"] = milliseconds_since_epoch (status.election_end);
				election["tally"] = status.tally.to_string_dec ();
				election["final"] = status.final_tally.to_string_dec ();
				election["blocks"] = std::to_string (status.block_count);
				election["voters"] = std::to_string (status.voter_count);
				election["request_count"] = std::to_string (status.confirmation_request_count);
				elections.push_back (std::move (election));
			}
			running_total += status.election_duration;
		}
	}
	confirmation_stats["count"] = static_cast<std::uint64_t> (elections.size ());
	if (elections.size () >= 1)
	{
		confirmation_stats["average"] = (running_total.count ()) / elections.size ();
	}
	response_l["confirmation_stats"] = std::move (confirmation_stats);
	response_l["confirmations"] = std::move (elections);
	response_errors ();
}

void nano::json_handler::confirmation_info ()
{
	bool const representatives = request.contains ("representatives") ? request.at ("representatives").as_bool () : false;
	bool const contents = request.contains ("contents") ? request.at ("contents").as_bool () : true;
	bool const json_block_l = request.contains ("json_block") ? request.at ("json_block").as_bool () : false;
	std::string root_text (request.at ("root").as_string ().c_str ());
	nano::qualified_root root;
	if (!root.decode_hex (root_text))
	{
		auto election (node.active.election (root));
		if (election != nullptr && !election->confirmed ())
		{
			auto info = election->current_status ();
			response_l["announcements"] = std::to_string (info.status.confirmation_request_count);
			response_l["voters"] = std::to_string (info.votes.size ());
			response_l["last_winner"] = info.status.winner->hash ().to_string ();
			nano::uint128_t total (0);
			boost::json::object blocks;
			for (auto const & [tally, block] : info.tally)
			{
				boost::json::object entry;
				entry["tally"] = tally.convert_to<std::string> ();
				total += tally;
				if (contents)
				{
					if (json_block_l)
					{
						boost::json::object block_node_l;
						block->serialize_json (block_node_l);
						entry["contents"] = std::move (block_node_l);
					}
					else
					{
						std::string contents;
						block->serialize_json (contents);
						entry["contents"] = contents;
					}
				}
				if (representatives)
				{
					std::multimap<nano::uint128_t, nano::account, std::greater<nano::uint128_t>> representatives;
					std::multimap<nano::uint128_t, nano::account, std::greater<nano::uint128_t>> representatives_final;
					for (auto const & [representative, vote] : info.votes)
					{
						if (block->hash () == vote.hash)
						{
							auto amount (node.ledger.weight (representative));
							representatives.emplace (amount, representative);
							if (vote.timestamp == std::numeric_limits<uint64_t>::max ())
							{
								representatives_final.emplace (amount, representative);
							}
						}
					}

					boost::json::object representatives_ptree;
					boost::json::object representatives_ptree_final;
					for (auto const & [amount, representative] : representatives)
					{
						representatives_ptree[representative.to_account ()] = amount.convert_to<std::string> ();
					}
					for (auto const & [amount, representative] : representatives_final)
					{
						representatives_ptree_final[representative.to_account ()] = amount.convert_to<std::string> ();
					}

					entry["representatives"] = std::move (representatives_ptree);
					entry["representatives_final"] = std::move (representatives_ptree_final);
				}
				blocks[(block->hash ()).to_string ()] = std::move (entry);
			}
			response_l["total_tally"] = total.convert_to<std::string> ();
			response_l["final_tally"] = info.status.final_tally.to_string_dec ();
			response_l["blocks"] = std::move (blocks);
		}
		else
		{
			ec = nano::error_rpc::confirmation_not_found;
		}
	}
	else
	{
		ec = nano::error_rpc::invalid_root;
	}
	response_errors ();
}

void nano::json_handler::confirmation_quorum ()
{
	response_l["quorum_delta"] = node.online_reps.delta ().convert_to<std::string> ();
	response_l["online_weight_quorum_percent"] = std::to_string (node.online_reps.online_weight_quorum);
	response_l["online_weight_minimum"] = node.config.online_weight_minimum.to_string_dec ();
	response_l["online_stake_total"] = node.online_reps.online ().convert_to<std::string> ();
	response_l["trended_stake_total"] = node.online_reps.trended ().convert_to<std::string> ();
	response_l["peers_stake_total"] = node.rep_crawler.total_weight ().convert_to<std::string> ();
	if (request.contains ("peer_details") ? request.at ("peer_details").as_bool () : false)
	{
		boost::json::array peers;
		for (auto & peer : node.rep_crawler.representatives ())
		{
			boost::json::object peer_node;
			peer_node["account"] = peer.account.to_account ();
			peer_node["ip"] = peer.channel->to_string ();
			peer_node["weight"] = nano::amount{ node.ledger.weight (peer.account) }.to_string_dec ();
			peers.push_back (std::move (peer_node));
		}
		response_l["peers"] = std::move (peers);
	}
	response_errors ();
}

void nano::json_handler::database_txn_tracker ()
{
	if (node.config.txn_tracking.enable)
	{
		unsigned min_read_time_milliseconds = 0;
		if (auto* min_read_time_val = request.if_contains ("min_read_time"))
		{
			std::string min_read_time_text (min_read_time_val->as_string ().c_str ());
			auto success = boost::conversion::try_lexical_convert<unsigned> (min_read_time_text, min_read_time_milliseconds);
			if (!success)
			{
				ec = nano::error_common::invalid_amount;
			}
		}

		unsigned min_write_time_milliseconds = 0;
		if (!ec)
		{
			if (auto* min_write_time_val = request.if_contains ("min_write_time"))
			{
				std::string min_write_time_text (min_write_time_val->as_string ().c_str ());
				auto success = boost::conversion::try_lexical_convert<unsigned> (min_write_time_text, min_write_time_milliseconds);
				if (!success)
				{
					ec = nano::error_common::invalid_amount;
				}
			}
		}

		if (!ec)
		{
			boost::json::object txn_tracking;
			node.store.backend.collect_txn_tracker (txn_tracking, std::chrono::milliseconds (min_read_time_milliseconds), std::chrono::milliseconds (min_write_time_milliseconds));
			response_l["txn_tracking"] = std::move (txn_tracking);
		}
	}
	else
	{
		ec = nano::error_common::tracking_not_enabled;
	}

	response_errors ();
}

void nano::json_handler::delegators ()
{
	auto representative (account_impl ());
	auto count (count_optional_impl (1024));
	auto threshold (threshold_optional_impl ());
	nano::account start_account{};
	if (auto* start_account_val = request.if_contains ("start"))
	{
		std::string start_account_text (start_account_val->as_string ().c_str ());
		if (!ec)
		{
			start_account = account_impl (start_account_text);
		}
	}

	if (!ec)
	{
		auto transaction (node.ledger.tx_begin_read ());
		boost::json::object delegators;
		for (auto i (node.store.account.begin (transaction, inc_sat (start_account.number ()))), n (node.store.account.end (transaction)); i != n && delegators.size () < count; ++i)
		{
			nano::account_info const & info (i->second);
			if (info.representative == representative)
			{
				if (info.balance.number () >= threshold.number ())
				{
					std::string balance = nano::uint128_union (info.balance).to_string_dec ();
					nano::account const & delegator (i->first);
					delegators[delegator.to_account ()] = balance;
				}
			}
		}
		response_l["delegators"] = std::move (delegators);
	}
	response_errors ();
}

void nano::json_handler::delegators_count ()
{
	auto account (account_impl ());
	if (!ec)
	{
		uint64_t count (0);
		auto transaction (node.ledger.tx_begin_read ());
		for (auto i (node.store.account.begin (transaction)), n (node.store.account.end (transaction)); i != n; ++i)
		{
			nano::account_info const & info (i->second);
			if (info.representative == account)
			{
				++count;
			}
		}
		response_l["count"] = std::to_string (count);
	}
	response_errors ();
}

void nano::json_handler::deterministic_key ()
{
	std::string seed_text (request.at ("seed").as_string ().c_str ());
	std::string index_text (request.at ("index").as_string ().c_str ());
	nano::raw_key seed;
	if (!seed.decode_hex (seed_text))
	{
		try
		{
			uint32_t index (std::stoul (index_text));
			nano::raw_key prv = nano::deterministic_key (seed, index);
			nano::public_key pub (nano::pub_key (prv));
			response_l["private"] = prv.to_string ();
			response_l["public"] = pub.to_string ();
			response_l["account"] = pub.to_account ();
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

/*
 * @warning This is an internal/diagnostic RPC, do not rely on its interface being stable
 */
void nano::json_handler::epoch_upgrade ()
{
	nano::epoch epoch (nano::epoch::invalid);
	uint8_t epoch_int (static_cast<uint8_t> (request.at ("epoch").as_int64 ()));
	switch (epoch_int)
	{
		case 1:
			epoch = nano::epoch::epoch_1;
			break;
		case 2:
			epoch = nano::epoch::epoch_2;
			break;
		default:
			break;
	}
	if (epoch != nano::epoch::invalid)
	{
		uint64_t count_limit (count_optional_impl ());
		uint64_t threads (0);
		if (auto* threads_val = request.if_contains ("threads"))
		{
			std::string threads_text (threads_val->as_string ().c_str ());
			if (!ec && decode_unsigned (threads_text, threads))
			{
				ec = nano::error_rpc::invalid_threads_count;
			}
		}
		std::string key_text (request.at ("key").as_string ().c_str ());
		nano::raw_key prv;
		if (!prv.decode_hex (key_text))
		{
			if (nano::pub_key (prv) == node.ledger.epoch_signer (node.ledger.epoch_link (epoch)))
			{
				if (!node.epoch_upgrader.start (prv, epoch, count_limit, threads))
				{
					response_l["started"] = "1";
				}
				else
				{
					response_l["started"] = "0";
				}
			}
			else
			{
				ec = nano::error_rpc::invalid_epoch_signer;
			}
		}
		else
		{
			ec = nano::error_common::bad_private_key;
		}
	}
	else
	{
		ec = nano::error_rpc::invalid_epoch;
	}
	response_errors ();
}

void nano::json_handler::frontiers ()
{
	auto start (account_impl ());
	auto count (count_impl ());
	if (!ec)
	{
		boost::json::object frontiers;
		auto transaction (node.ledger.tx_begin_read ());
		for (auto i (node.store.account.begin (transaction, start)), n (node.store.account.end (transaction)); i != n && frontiers.size () < count; ++i)
		{
			frontiers[i->first.to_account ()] = i->second.head.to_string ();
		}
		response_l["frontiers"] = std::move (frontiers);
	}
	response_errors ();
}

void nano::json_handler::account_count ()
{
	auto size (node.ledger.account_count ());
	response_l["count"] = std::to_string (size);
	response_errors ();
}

namespace
{
class history_visitor : public nano::block_visitor
{
public:
	history_visitor (nano::json_handler & handler_a, bool raw_a, nano::secure::transaction & transaction_a, boost::json::object & tree_a, nano::block_hash const & hash_a, std::vector<nano::public_key> const & accounts_filter_a) :
		handler (handler_a),
		raw (raw_a),
		transaction (transaction_a),
		tree (tree_a),
		hash (hash_a),
		accounts_filter (accounts_filter_a)
	{
	}
	virtual ~history_visitor () = default;
	void send_block (nano::send_block const & block_a)
	{
		if (should_ignore_account (block_a.hashables.destination))
		{
			return;
		}
		tree["type"] = "send";
		auto account (block_a.hashables.destination.to_account ());
		tree["account"] = account;
		auto amount = handler.node.ledger.any.block_amount (transaction, hash);
		if (amount)
		{
			tree["amount"] = amount.value ().number ().convert_to<std::string> ();
		}
		if (raw)
		{
			tree["destination"] = account;
			tree["balance"] = block_a.hashables.balance.to_string_dec ();
			tree["previous"] = block_a.hashables.previous.to_string ();
		}
	}
	void receive_block (nano::receive_block const & block_a)
	{
		tree["type"] = "receive";
		auto amount = handler.node.ledger.any.block_amount (transaction, hash);
		if (amount)
		{
			auto source_account = handler.node.ledger.any.block_account (transaction, block_a.hashables.source);
			if (source_account)
			{
				tree["account"] = source_account.value ().to_account ();
			}
			tree["amount"] = amount.value ().number ().convert_to<std::string> ();
		}
		if (raw)
		{
			tree["source"] = block_a.hashables.source.to_string ();
			tree["previous"] = block_a.hashables.previous.to_string ();
		}
	}
	void open_block (nano::open_block const & block_a)
	{
		if (raw)
		{
			tree["type"] = "open";
			tree["representative"] = block_a.hashables.representative.to_account ();
			tree["source"] = block_a.hashables.source.to_string ();
			tree["opened"] = block_a.hashables.account.to_account ();
		}
		else
		{
			// Report opens as a receive
			tree["type"] = "receive";
		}
		if (block_a.hashables.source != handler.node.ledger.constants.genesis->account ().as_union ())
		{
			bool error_or_pruned (false);
			auto amount = handler.node.ledger.any.block_amount (transaction, hash);
			if (amount)
			{
				auto source_account = handler.node.ledger.any.block_account (transaction, block_a.hashables.source);
				if (source_account)
				{
					tree["account"] = source_account.value ().to_account ();
				}
				tree["amount"] = amount.value ().number ().convert_to<std::string> ();
			}
		}
		else
		{
			tree["account"] = handler.node.ledger.constants.genesis->account ().to_account ();
			tree["amount"] = nano::dev::constants.genesis_amount.convert_to<std::string> ();
		}
	}
	void change_block (nano::change_block const & block_a)
	{
		if (raw && accounts_filter.empty ())
		{
			tree["type"] = "change";
			tree["representative"] = block_a.hashables.representative.to_account ();
			tree["previous"] = block_a.hashables.previous.to_string ();
		}
	}
	void state_block (nano::state_block const & block_a)
	{
		if (raw)
		{
			tree["type"] = "state";
			tree["representative"] = block_a.hashables.representative.to_account ();
			tree["link"] = block_a.hashables.link.to_string ();
			tree["balance"] = block_a.hashables.balance.to_string_dec ();
			tree["previous"] = block_a.hashables.previous.to_string ();
		}
		auto balance (block_a.hashables.balance.number ());
		auto previous_balance_raw = handler.node.ledger.any.block_balance (transaction, block_a.hashables.previous);
		auto previous_balance = previous_balance_raw.value_or (0);
		if (!block_a.hashables.previous.is_zero () && !previous_balance_raw.has_value ())
		{
			// If previous hash is non-zero and we can't query the balance, e.g. it's pruned, we can't determine the block type
			if (raw)
			{
				tree["subtype"] = "unknown";
			}
			else
			{
				tree["type"] = "unknown";
			}
		}
		else if (balance < previous_balance.number ())
		{
			if (should_ignore_account (block_a.hashables.link.as_account ()))
			{
				tree.clear ();
				return;
			}
			if (raw)
			{
				tree["subtype"] = "send";
			}
			else
			{
				tree["type"] = "send";
			}
			tree["account"] = block_a.hashables.link.to_account ();
			tree["amount"] = (previous_balance.number () - balance).convert_to<std::string> ();
		}
		else
		{
			if (block_a.hashables.link.is_zero ())
			{
				if (raw && accounts_filter.empty ())
				{
					tree["subtype"] = "change";
				}
			}
			else if (balance == previous_balance && handler.node.ledger.is_epoch_link (block_a.hashables.link))
			{
				if (raw && accounts_filter.empty ())
				{
					tree["subtype"] = "epoch";
					tree["account"] = handler.node.ledger.epoch_signer (block_a.link_field ().value ()).to_account ();
				}
			}
			else
			{
				auto source_account = handler.node.ledger.any.block_account (transaction, block_a.hashables.link.as_block_hash ());
				if (source_account && should_ignore_account (source_account.value ()))
				{
					tree.clear ();
					return;
				}
				if (raw)
				{
					tree["subtype"] = "receive";
				}
				else
				{
					tree["type"] = "receive";
				}
				if (source_account)
				{
					tree["account"] = source_account.value ().to_account ();
				}
				tree["amount"] = (balance - previous_balance.number ()).convert_to<std::string> ();
			}
		}
	}
	bool should_ignore_account (nano::public_key const & account)
	{
		bool ignore (false);
		if (!accounts_filter.empty ())
		{
			if (std::find (accounts_filter.begin (), accounts_filter.end (), account) == accounts_filter.end ())
			{
				ignore = true;
			}
		}
		return ignore;
	}
	nano::json_handler & handler;
	bool raw;
	nano::secure::transaction & transaction;
	boost::json::object & tree;
	nano::block_hash const & hash;
	std::vector<nano::public_key> const & accounts_filter;
};
}

void nano::json_handler::account_history ()
{
	std::vector<nano::public_key> accounts_to_filter;
	if (auto* accounts_filter_node = request.if_contains ("account_filter"))
	{
		for (auto & a : accounts_filter_node->as_array ())
		{
			auto account (account_impl (std::string (a.as_string ().c_str ())));
			if (!ec)
			{
				accounts_to_filter.push_back (account);
			}
			else
			{
				break;
			}
		}
	}
	nano::account account;
	nano::block_hash hash;
	bool reverse = false;
	if (auto* reverse_val = request.if_contains ("reverse"))
	{
		reverse = reverse_val->as_bool ();
	}
	std::optional<std::string> head_str;
	if (auto* head_val = request.if_contains ("head"))
	{
		head_str = std::string (head_val->as_string ().c_str ());
	}
	auto transaction = node.ledger.tx_begin_read ();
	auto count (count_impl ());
	auto offset (offset_optional_impl (0));
	if (head_str)
	{
		if (!hash.decode_hex (*head_str))
		{
			if (node.ledger.any.block_exists (transaction, hash))
			{
				account = node.ledger.any.block_account (transaction, hash).value ();
			}
			else
			{
				ec = nano::error_blocks::not_found;
			}
		}
		else
		{
			ec = nano::error_blocks::bad_hash_number;
		}
	}
	else
	{
		account = account_impl ();
		if (!ec)
		{
			if (reverse)
			{
				auto info (account_info_impl (transaction, account));
				if (!ec)
				{
					hash = info.open_block;
				}
			}
			else
			{
				hash = node.ledger.any.account_head (transaction, account);
			}
		}
	}
	if (!ec)
	{
		boost::json::array history;
		bool include_linked_account = false;
		if (auto* val = request.if_contains ("include_linked_account"))
		{
			include_linked_account = val->as_bool ();
		}
		bool output_raw = false;
		if (auto* val = request.if_contains ("raw"))
		{
			output_raw = val->as_bool ();
		}
		response_l["account"] = account.to_account ();
		auto block = node.ledger.any.block_get (transaction, hash);
		while (block != nullptr && count > 0)
		{
			if (offset > 0)
			{
				--offset;
			}
			else
			{
				boost::json::object entry;
				history_visitor visitor (*this, output_raw, transaction, entry, hash, accounts_to_filter);
				block->visit (visitor);
				if (!entry.empty ())
				{
					if (include_linked_account)
					{
						auto linked_account = node.ledger.linked_account (transaction, *block);
						if (linked_account.has_value ())
						{
							entry["linked_account"] = linked_account.value ().to_account ();
						}
						else
						{
							entry["linked_account"] = "0";
						}
					}
					entry["local_timestamp"] = std::to_string (block->sideband ().timestamp);
					entry["height"] = std::to_string (block->sideband ().height);
					entry["hash"] = hash.to_string ();
					entry["confirmed"] = node.ledger.confirmed.block_exists_or_pruned (transaction, hash);
					if (output_raw)
					{
						entry["work"] = nano::to_string_hex (block->block_work ());
						entry["signature"] = block->block_signature ().to_string ();
					}
					history.push_back (std::move (entry));
					--count;
				}
			}
			hash = reverse ? node.ledger.any.block_successor (transaction, hash).value_or (0) : block->previous ();
			block = node.ledger.any.block_get (transaction, hash);
		}
		response_l["history"] = std::move (history);
		if (!hash.is_zero ())
		{
			response_l[reverse ? "next" : "previous"] = hash.to_string ();
		}
	}
	response_errors ();
}

void nano::json_handler::keepalive ()
{
	if (!ec)
	{
		std::string address_text (request.at ("address").as_string ().c_str ());
		std::string port_text (request.at ("port").as_string ().c_str ());
		uint16_t port;
		if (!nano::parse_port (port_text, port))
		{
			node.keepalive (address_text, port);
			response_l["started"] = "1";
		}
		else
		{
			ec = nano::error_common::invalid_port;
		}
	}
	response_errors ();
}

void nano::json_handler::key_create ()
{
	nano::keypair pair;
	response_l["private"] = pair.prv.to_string ();
	response_l["public"] = pair.pub.to_string ();
	response_l["account"] = pair.pub.to_account ();
	response_errors ();
}

void nano::json_handler::key_expand ()
{
	std::string key_text (request.at ("key").as_string ().c_str ());
	nano::raw_key prv;
	if (!prv.decode_hex (key_text))
	{
		nano::public_key pub (nano::pub_key (prv));
		response_l["private"] = prv.to_string ();
		response_l["public"] = pub.to_string ();
		response_l["account"] = pub.to_account ();
	}
	else
	{
		ec = nano::error_common::bad_private_key;
	}
	response_errors ();
}

void nano::json_handler::ledger ()
{
	auto count (count_optional_impl ());
	auto threshold (threshold_optional_impl ());
	if (!ec)
	{
		nano::account start{};
		if (auto* account_val = request.if_contains ("account"))
		{
			std::string account_text (account_val->as_string ().c_str ());
			start = account_impl (account_text);
		}
		uint64_t modified_since (0);
		if (auto* modified_since_val = request.if_contains ("modified_since"))
		{
			std::string modified_since_text (modified_since_val->as_string ().c_str ());
			if (decode_unsigned (modified_since_text, modified_since))
			{
				ec = nano::error_rpc::invalid_timestamp;
			}
		}
		bool const sorting = request.contains ("sorting") ? request.at ("sorting").as_bool () : false;
		bool const representative = request.contains ("representative") ? request.at ("representative").as_bool () : false;
		bool const weight = request.contains ("weight") ? request.at ("weight").as_bool () : false;
		bool const pending = request.contains ("pending") ? request.at ("pending").as_bool () : false;
		bool const receivable = request.contains ("receivable") ? request.at ("receivable").as_bool () : pending;
		boost::json::object accounts;
		auto transaction = node.ledger.tx_begin_read ();
		if (!ec && !sorting) // Simple
		{
			for (auto i (node.store.account.begin (transaction, start)), n (node.store.account.end (transaction)); i != n && accounts.size () < count; ++i)
			{
				nano::account_info const & info (i->second);
				if (info.modified >= modified_since && (receivable || info.balance.number () >= threshold.number ()))
				{
					nano::account const & account (i->first);
					boost::json::object response_a;
					if (receivable)
					{
						auto account_receivable = node.ledger.account_receivable (transaction, account);
						if (info.balance.number () + account_receivable < threshold.number ())
						{
							continue;
						}
						response_a["pending"] = account_receivable.convert_to<std::string> ();
						response_a["receivable"] = account_receivable.convert_to<std::string> ();
					}
					response_a["frontier"] = info.head.to_string ();
					response_a["open_block"] = info.open_block.to_string ();
					response_a["representative_block"] = node.ledger.representative_block (transaction, info.head).to_string ();
					std::string balance = nano::uint128_union (info.balance).to_string_dec ();
					response_a["balance"] = balance;
					response_a["modified_timestamp"] = std::to_string (info.modified);
					response_a["block_count"] = std::to_string (info.block_count);
					if (representative)
					{
						response_a["representative"] = info.representative.to_account ();
					}
					if (weight)
					{
						auto account_weight (node.ledger.weight_exact (transaction, account));
						response_a["weight"] = account_weight.convert_to<std::string> ();
					}
					accounts[account.to_account ()] = std::move (response_a);
				}
			}
		}
		else if (!ec) // Sorting
		{
			std::vector<std::pair<nano::uint128_union, nano::account>> ledger_l;
			for (auto i (node.store.account.begin (transaction, start)), n (node.store.account.end (transaction)); i != n; ++i)
			{
				nano::account_info const & info (i->second);
				nano::uint128_union balance (info.balance);
				if (info.modified >= modified_since)
				{
					ledger_l.emplace_back (balance, i->first);
				}
			}
			std::sort (ledger_l.begin (), ledger_l.end ());
			std::reverse (ledger_l.begin (), ledger_l.end ());
			nano::account_info info;
			for (auto i (ledger_l.begin ()), n (ledger_l.end ()); i != n && accounts.size () < count; ++i)
			{
				node.store.account.get (transaction, i->second, info);
				if (receivable || info.balance.number () >= threshold.number ())
				{
					nano::account const & account (i->second);
					boost::json::object response_a;
					if (receivable)
					{
						auto account_receivable = node.ledger.account_receivable (transaction, account);
						if (info.balance.number () + account_receivable < threshold.number ())
						{
							continue;
						}
						response_a["pending"] = account_receivable.convert_to<std::string> ();
						response_a["receivable"] = account_receivable.convert_to<std::string> ();
					}
					response_a["frontier"] = info.head.to_string ();
					response_a["open_block"] = info.open_block.to_string ();
					response_a["representative_block"] = node.ledger.representative_block (transaction, info.head).to_string ();
					std::string balance = (i->first).to_string_dec ();
					response_a["balance"] = balance;
					response_a["modified_timestamp"] = std::to_string (info.modified);
					response_a["block_count"] = std::to_string (info.block_count);
					if (representative)
					{
						response_a["representative"] = info.representative.to_account ();
					}
					if (weight)
					{
						auto account_weight (node.ledger.weight_exact (transaction, account));
						response_a["weight"] = account_weight.convert_to<std::string> ();
					}
					accounts[account.to_account ()] = std::move (response_a);
				}
			}
		}
		response_l["accounts"] = std::move (accounts);
	}
	response_errors ();
}

void nano::json_handler::nano_to_raw ()
{
	auto amount (amount_impl ());
	if (!ec)
	{
		auto result (amount.number () * nano::nano_ratio);
		if (result > amount.number ())
		{
			response_l["amount"] = result.convert_to<std::string> ();
		}
		else
		{
			ec = nano::error_common::invalid_amount_big;
		}
	}
	response_errors ();
}

void nano::json_handler::raw_to_nano ()
{
	auto amount (amount_impl ());
	if (!ec)
	{
		auto result (amount.number () / nano::nano_ratio);
		response_l["amount"] = result.convert_to<std::string> ();
	}
	response_errors ();
}

/*
 * @warning This is an internal/diagnostic RPC, do not rely on its interface being stable
 */
void nano::json_handler::node_id ()
{
	if (!ec)
	{
		response_l["public"] = node.node_id.pub.to_string ();
		response_l["as_account"] = node.node_id.pub.to_account ();
		response_l["node_id"] = node.node_id.pub.to_node_id ();
	}
	response_errors ();
}

/*
 * @warning This is an internal/diagnostic RPC, do not rely on its interface being stable
 */
void nano::json_handler::node_id_delete ()
{
	response_l["deprecated"] = "1";
	response_errors ();
}

void nano::json_handler::password_change ()
{
	node.workers.post (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		auto wallet (rpc_l->wallet_impl ());
		if (!rpc_l->ec)
		{
			rpc_l->wallet_locked_impl (wallet);
			if (!rpc_l->ec)
			{
				std::string password_text (rpc_l->request.at ("password").as_string ().c_str ());
				bool error (wallet->rekey (password_text));
				rpc_l->response_l["changed"] = error ? "0" : "1";
				if (!error)
				{
					rpc_l->node.logger.warn (nano::log::type::rpc, "Wallet password changed");
				}
			}
		}
		rpc_l->response_errors ();
	}));
}

void nano::json_handler::password_enter ()
{
	node.workers.post (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		auto wallet (rpc_l->wallet_impl ());
		if (!rpc_l->ec)
		{
			std::string password_text (rpc_l->request.at ("password").as_string ().c_str ());
			auto error (wallet->enter_password (password_text));
			rpc_l->response_l["valid"] = error ? "0" : "1";
		}
		rpc_l->response_errors ();
	}));
}

void nano::json_handler::password_valid (bool wallet_locked)
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto valid (!wallet->is_locked ());
		if (!wallet_locked)
		{
			response_l["valid"] = valid ? "1" : "0";
		}
		else
		{
			response_l["locked"] = valid ? "0" : "1";
		}
	}
	response_errors ();
}

void nano::json_handler::peers ()
{
	boost::json::object peers_l;
	bool const peer_details = request.contains ("peer_details") ? request.at ("peer_details").as_bool () : false;
	auto peers_list (node.network.list (std::numeric_limits<std::size_t>::max ()));
	std::sort (peers_list.begin (), peers_list.end (), [] (auto const & lhs, auto const & rhs) {
		return lhs->get_remote_endpoint () < rhs->get_remote_endpoint ();
	});
	for (auto i (peers_list.begin ()), n (peers_list.end ()); i != n; ++i)
	{
		std::stringstream text;
		auto channel (*i);
		text << channel->to_string ();
		if (peer_details)
		{
			boost::json::object pending_tree;
			pending_tree["protocol_version"] = std::to_string (channel->get_network_version ());
			auto node_id_l (channel->get_node_id_optional ());
			if (node_id_l.has_value ())
			{
				pending_tree["node_id"] = node_id_l.value ().to_node_id ();
			}
			else
			{
				pending_tree["node_id"] = "";
			}
			debug_assert (channel->get_type () == nano::transport::transport_type::tcp);
			pending_tree["type"] = "tcp";

			auto peering_endpoint = channel->get_peering_endpoint ();
			pending_tree["peering"] = boost::lexical_cast<std::string> (peering_endpoint);

			peers_l[text.str ()] = std::move (pending_tree);
		}
		else
		{
			peers_l[text.str ()] = std::to_string (channel->get_network_version ());
		}
	}
	response_l["peers"] = std::move (peers_l);
	response_errors ();
}

void nano::json_handler::pending ()
{
	response_l["deprecated"] = "1";
	receivable ();
}

void nano::json_handler::receivable ()
{
	auto account (account_impl ());
	auto count (count_optional_impl ());
	auto offset (offset_optional_impl (0));
	auto threshold (threshold_optional_impl ());
	bool const source = request.contains ("source") ? request.at ("source").as_bool () : false;
	bool const min_version = request.contains ("min_version") ? request.at ("min_version").as_bool () : false;
	bool const include_active = request.contains ("include_active") ? request.at ("include_active").as_bool () : false;
	bool const include_only_confirmed = request.contains ("include_only_confirmed") ? request.at ("include_only_confirmed").as_bool () : true;
	bool const sorting = request.contains ("sorting") ? request.at ("sorting").as_bool () : false;
	auto simple (threshold.is_zero () && !source && !min_version && !sorting); // if simple, response is a list of hashes
	bool const should_sort = sorting && !simple;
	if (!ec)
	{
		auto offset_counter = offset;
		auto transaction = node.ledger.tx_begin_read ();
		if (simple)
		{
			// Simple case: return an array of hashes
			boost::json::array blocks_arr;
			for (auto i (node.store.pending.begin (transaction, nano::pending_key (account, 0))), n (node.store.pending.end (transaction)); i != n && nano::pending_key (i->first).account == account && blocks_arr.size () < count; ++i)
			{
				nano::pending_key const & key (i->first);
				if (block_confirmed (node, transaction, key.hash, include_active, include_only_confirmed))
				{
					if (offset_counter > 0)
					{
						--offset_counter;
						continue;
					}
					blocks_arr.push_back (boost::json::value (key.hash.to_string ()));
				}
			}
			response_l["blocks"] = std::move (blocks_arr);
		}
		else
		{
			// Complex case: return an object with hash keys
			boost::json::object peers_l;
			// The ptree container is used if there are any children nodes (e.g source/min_version) otherwise the amount container is used.
			std::vector<std::pair<std::string, boost::json::object>> hash_ptree_pairs;
			std::vector<std::pair<std::string, nano::uint128_t>> hash_amount_pairs;
			for (auto i (node.store.pending.begin (transaction, nano::pending_key (account, 0))), n (node.store.pending.end (transaction)); i != n && nano::pending_key (i->first).account == account && (should_sort || peers_l.size () < count); ++i)
			{
				nano::pending_key const & key (i->first);
				if (block_confirmed (node, transaction, key.hash, include_active, include_only_confirmed))
				{
					if (!should_sort && offset_counter > 0)
					{
						--offset_counter;
						continue;
					}

					nano::pending_info const & info (i->second);
					if (info.amount.number () >= threshold.number ())
					{
						if (source || min_version)
						{
							boost::json::object pending_tree;
							pending_tree["amount"] = info.amount.number ().convert_to<std::string> ();
							if (source)
							{
								pending_tree["source"] = info.source.to_account ();
							}
							if (min_version)
							{
								pending_tree["min_version"] = epoch_as_string (info.epoch);
							}

							if (should_sort)
							{
								hash_ptree_pairs.emplace_back (key.hash.to_string (), pending_tree);
							}
							else
							{
								peers_l[key.hash.to_string ()] = std::move (pending_tree);
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
								peers_l[key.hash.to_string ()] = info.amount.number ().convert_to<std::string> ();
							}
						}
					}
				}
			}
			if (should_sort)
			{
				if (source || min_version)
				{
					std::stable_sort (hash_ptree_pairs.begin (), hash_ptree_pairs.end (), [] (auto const & lhs, auto const & rhs) {
						nano::amount lhs_amount;
						nano::amount rhs_amount;
						lhs_amount.decode_dec (std::string (lhs.second.at ("amount").as_string ().c_str ()));
						rhs_amount.decode_dec (std::string (rhs.second.at ("amount").as_string ().c_str ()));
						return lhs_amount.number () > rhs_amount.number ();
					});
					for (auto i = offset, j = offset + count; i < hash_ptree_pairs.size () && i < j; ++i)
					{
						peers_l[hash_ptree_pairs[i].first] = hash_ptree_pairs[i].second;
					}
				}
				else
				{
					std::stable_sort (hash_amount_pairs.begin (), hash_amount_pairs.end (), [] (auto const & lhs, auto const & rhs) {
						return lhs.second > rhs.second;
					});

					for (auto i = offset, j = offset + count; i < hash_amount_pairs.size () && i < j; ++i)
					{
						peers_l[hash_amount_pairs[i].first] = hash_amount_pairs[i].second.convert_to<std::string> ();
					}
				}
			}
			response_l["blocks"] = std::move (peers_l);
		}
	}
	response_errors ();
}

void nano::json_handler::pending_exists ()
{
	response_l["deprecated"] = "1";
	receivable_exists ();
}

void nano::json_handler::receivable_exists ()
{
	auto hash (hash_impl ());
	bool const include_active = request.contains ("include_active") ? request.at ("include_active").as_bool () : false;
	bool const include_only_confirmed = request.contains ("include_only_confirmed") ? request.at ("include_only_confirmed").as_bool () : true;
	if (!ec)
	{
		auto transaction = node.ledger.tx_begin_read ();
		auto block = node.ledger.any.block_get (transaction, hash);
		if (block != nullptr)
		{
			auto exists (false);
			if (block->is_send ())
			{
				exists = node.ledger.any.pending_get (transaction, nano::pending_key{ block->destination (), hash }).has_value ();
			}
			exists = exists && (block_confirmed (node, transaction, block->hash (), include_active, include_only_confirmed));
			response_l["exists"] = exists ? "1" : "0";
		}
		else
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void nano::json_handler::process ()
{
	node.workers.post (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		bool const is_async = rpc_l->request.contains ("async") ? rpc_l->request.at ("async").as_bool () : false;
		auto block (rpc_l->block_impl (true));

		// State blocks subtype check
		if (!rpc_l->ec && block->type () == nano::block_type::state)
		{
			std::string subtype_text (rpc_l->request.contains ("subtype") ? rpc_l->request.at ("subtype").as_string ().c_str () : "");
			if (!subtype_text.empty ())
			{
				std::shared_ptr<nano::state_block> block_state (std::static_pointer_cast<nano::state_block> (block));
				auto transaction = rpc_l->node.ledger.tx_begin_read ();
				if (!block_state->hashables.previous.is_zero () && !rpc_l->node.ledger.any.block_exists (transaction, block_state->hashables.previous))
				{
					rpc_l->ec = nano::error_process::gap_previous;
				}
				else
				{
					auto balance (rpc_l->node.ledger.any.account_balance (transaction, block_state->hashables.account).value_or (0).number ());
					if (subtype_text == "send")
					{
						if (balance <= block_state->hashables.balance.number ())
						{
							rpc_l->ec = nano::error_rpc::invalid_subtype_balance;
						}
						// Send with previous == 0 fails balance check. No previous != 0 check required
					}
					else if (subtype_text == "receive")
					{
						if (balance > block_state->hashables.balance.number ())
						{
							rpc_l->ec = nano::error_rpc::invalid_subtype_balance;
						}
						// Receive can be point to open block. No previous != 0 check required
					}
					else if (subtype_text == "open")
					{
						if (!block_state->hashables.previous.is_zero ())
						{
							rpc_l->ec = nano::error_rpc::invalid_subtype_previous;
						}
					}
					else if (subtype_text == "change")
					{
						if (balance != block_state->hashables.balance.number ())
						{
							rpc_l->ec = nano::error_rpc::invalid_subtype_balance;
						}
						else if (block_state->hashables.previous.is_zero ())
						{
							rpc_l->ec = nano::error_rpc::invalid_subtype_previous;
						}
					}
					else if (subtype_text == "epoch")
					{
						if (balance != block_state->hashables.balance.number ())
						{
							rpc_l->ec = nano::error_rpc::invalid_subtype_balance;
						}
						else if (!rpc_l->node.ledger.is_epoch_link (block_state->hashables.link))
						{
							rpc_l->ec = nano::error_rpc::invalid_subtype_epoch_link;
						}
					}
					else
					{
						rpc_l->ec = nano::error_rpc::invalid_subtype;
					}
				}
			}
		}
		if (!rpc_l->ec)
		{
			if (!rpc_l->node.network_params.work.validate_entry (*block))
			{
				if (!is_async)
				{
					auto result_maybe = rpc_l->node.process_local (block);
					if (!result_maybe)
					{
						rpc_l->ec = nano::error_rpc::stopped;
					}
					else
					{
						auto const & result = result_maybe.value ();
						switch (result)
						{
							case nano::block_status::progress:
							{
								rpc_l->response_l["hash"] = block->hash ().to_string ();
								break;
							}
							case nano::block_status::gap_previous:
							{
								rpc_l->ec = nano::error_process::gap_previous;
								break;
							}
							case nano::block_status::gap_source:
							{
								rpc_l->ec = nano::error_process::gap_source;
								break;
							}
							case nano::block_status::old:
							{
								rpc_l->ec = nano::error_process::old;
								break;
							}
							case nano::block_status::bad_signature:
							{
								rpc_l->ec = nano::error_process::bad_signature;
								break;
							}
							case nano::block_status::negative_spend:
							{
								// TODO once we get RPC versioning, this should be changed to "negative spend"
								rpc_l->ec = nano::error_process::negative_spend;
								break;
							}
							case nano::block_status::balance_mismatch:
							{
								rpc_l->ec = nano::error_process::balance_mismatch;
								break;
							}
							case nano::block_status::unreceivable:
							{
								rpc_l->ec = nano::error_process::unreceivable;
								break;
							}
							case nano::block_status::block_position:
							{
								rpc_l->ec = nano::error_process::block_position;
								break;
							}
							case nano::block_status::gap_epoch_open_pending:
							{
								rpc_l->ec = nano::error_process::gap_epoch_open_pending;
								break;
							}
							case nano::block_status::fork:
							{
								bool const force = rpc_l->request.contains ("force") ? rpc_l->request.at ("force").as_bool () : false;
								if (force)
								{
									rpc_l->node.active.erase (*block);
									rpc_l->node.block_processor.force (block);
									rpc_l->response_l["hash"] = block->hash ().to_string ();
								}
								else
								{
									rpc_l->ec = nano::error_process::fork;
								}
								break;
							}
							case nano::block_status::insufficient_work:
							{
								rpc_l->ec = nano::error_process::insufficient_work;
								break;
							}
							case nano::block_status::opened_burn_account:
								rpc_l->ec = nano::error_process::opened_burn_account;
								break;
							default:
							{
								rpc_l->ec = nano::error_process::other;
								break;
							}
						}
					}
				}
				else
				{
					if (block->type () == nano::block_type::state)
					{
						rpc_l->node.process_local_async (block);
						rpc_l->response_l["started"] = "1";
					}
					else
					{
						rpc_l->ec = nano::error_common::is_not_state_block;
					}
				}
			}
			else
			{
				rpc_l->ec = nano::error_blocks::work_low;
			}
		}
		rpc_l->response_errors ();
	}));
}

void nano::json_handler::pruned_exists ()
{
	auto hash (hash_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		if (node.ledger.pruning)
		{
			auto exists (node.store.pruned.exists (transaction, hash));
			response_l["exists"] = exists ? "1" : "0";
		}
		else
		{
			ec = nano::error_rpc::pruning_disabled;
		}
	}
	response_errors ();
}

void nano::json_handler::receive ()
{
	auto wallet (wallet_impl ());
	auto account (account_impl ());
	auto hash (hash_impl ("block"));
	if (!ec)
	{
		wallet_locked_impl (wallet);
		wallet_account_impl (wallet, account);
		if (!ec)
		{
			auto block_transaction = node.ledger.tx_begin_read ();
			if (node.ledger.any.block_exists_or_pruned (block_transaction, hash))
			{
				auto pending_info = node.ledger.any.pending_get (block_transaction, nano::pending_key (account, hash));
				if (pending_info)
				{
					auto work (work_optional_impl ());
					if (!ec && work)
					{
						nano::root head;
						nano::epoch epoch = pending_info->epoch;
						auto info = node.ledger.any.account_get (block_transaction, account);
						if (info)
						{
							head = info->head;
							// When receiving, epoch version is the higher between the previous and the source blocks
							epoch = std::max (info->epoch (), epoch);
						}
						else
						{
							head = account;
						}
						nano::block_details details (epoch, false, true, false);
						if (node.network_params.work.difficulty (nano::work_version::work_1, head, work) < node.network_params.work.threshold (nano::work_version::work_1, details))
						{
							ec = nano::error_common::invalid_work;
						}
					}
					else if (!ec) // && work == 0
					{
						if (!node.work_generation_enabled ())
						{
							ec = nano::error_common::disabled_work_generation;
						}
					}
					if (!ec)
					{
						// Representative is only used by receive_action when opening accounts
						// Set a wallet default representative for new accounts
						nano::account representative (wallet->get_representative ());
						bool generate_work (work == 0); // Disable work generation if "work" option is provided
						auto response_a (response);
						wallet->receive_async (
						hash, representative, nano::dev::constants.genesis_amount, account, [response_a] (std::shared_ptr<nano::block> const & block_a) {
							if (block_a != nullptr)
							{
								boost::json::object response_l;
								response_l["block"] = block_a->hash ().to_string ();
								response_a (boost::json::serialize (response_l));
							}
							else
							{
								json_error_response (response_a, "Error generating block");
							}
						},
						work, generate_work);
					}
				}
				else
				{
					ec = nano::error_process::unreceivable;
				}
			}
			else
			{
				ec = nano::error_blocks::not_found;
			}
		}
	}
	// Because of receive_async
	if (ec)
	{
		response_errors ();
	}
}

void nano::json_handler::receive_minimum ()
{
	if (!ec)
	{
		response_l["amount"] = node.config.receive_minimum.to_string_dec ();
	}
	response_errors ();
}

void nano::json_handler::receive_minimum_set ()
{
	auto amount (amount_impl ());
	if (!ec)
	{
		node.config.receive_minimum = amount;
		response_l["success"] = "";
	}
	response_errors ();
}

void nano::json_handler::representatives ()
{
	auto count (count_optional_impl ());
	if (!ec)
	{
		bool const sorting = request.contains ("sorting") ? request.at ("sorting").as_bool () : false;
		boost::json::object representatives;
		auto rep_amounts = node.ledger.rep_weights.get_rep_amounts ();
		if (!sorting) // Simple
		{
			std::map<nano::account, nano::uint128_t> ordered (rep_amounts.begin (), rep_amounts.end ());
			for (auto & rep_amount : rep_amounts)
			{
				auto const & account (rep_amount.first);
				auto const & amount (rep_amount.second);
				representatives[account.to_account ()] = amount.convert_to<std::string> ();

				if (representatives.size () > count)
				{
					break;
				}
			}
		}
		else // Sorting
		{
			std::vector<std::pair<nano::uint128_t, std::string>> representation;

			for (auto & rep_amount : rep_amounts)
			{
				auto const & account (rep_amount.first);
				auto const & amount (rep_amount.second);
				representation.emplace_back (amount, account.to_account ());
			}
			std::sort (representation.begin (), representation.end ());
			std::reverse (representation.begin (), representation.end ());
			for (auto i (representation.begin ()), n (representation.end ()); i != n && representatives.size () < count; ++i)
			{
				representatives[i->second] = (i->first).convert_to<std::string> ();
			}
		}
		response_l["representatives"] = std::move (representatives);
	}
	response_errors ();
}

void nano::json_handler::representatives_online ()
{
	bool const weight = request.contains ("weight") ? request.at ("weight").as_bool () : false;
	std::vector<nano::public_key> accounts_to_filter;
	bool has_accounts_filter = false;
	if (auto* accounts_node = request.if_contains ("accounts"))
	{
		has_accounts_filter = true;
		for (auto & a : accounts_node->as_array ())
		{
			auto account (account_impl (std::string (a.as_string ().c_str ())));
			if (!ec)
			{
				accounts_to_filter.push_back (account);
			}
			else
			{
				break;
			}
		}
	}
	if (!ec)
	{
		auto reps (node.online_reps.list ());
		if (weight)
		{
			boost::json::object representatives;
			for (auto & i : reps)
			{
				if (has_accounts_filter)
				{
					if (accounts_to_filter.empty ())
					{
						break;
					}
					auto found_acc = std::find (accounts_to_filter.begin (), accounts_to_filter.end (), i);
					if (found_acc == accounts_to_filter.end ())
					{
						continue;
					}
					else
					{
						accounts_to_filter.erase (found_acc);
					}
				}
				boost::json::object weight_node;
				auto account_weight (node.ledger.weight (i));
				weight_node["weight"] = account_weight.convert_to<std::string> ();
				representatives[i.to_account ()] = std::move (weight_node);
			}
			response_l["representatives"] = std::move (representatives);
		}
		else
		{
			boost::json::array representatives;
			for (auto & i : reps)
			{
				if (has_accounts_filter)
				{
					if (accounts_to_filter.empty ())
					{
						break;
					}
					auto found_acc = std::find (accounts_to_filter.begin (), accounts_to_filter.end (), i);
					if (found_acc == accounts_to_filter.end ())
					{
						continue;
					}
					else
					{
						accounts_to_filter.erase (found_acc);
					}
				}
				representatives.push_back (boost::json::value (i.to_account ()));
			}
			response_l["representatives"] = std::move (representatives);
		}
	}
	response_errors ();
}

void nano::json_handler::republish ()
{
	auto count (count_optional_impl (1024U));
	uint64_t sources (0);
	uint64_t destinations (0);
	if (auto* sources_val = request.if_contains ("sources"))
	{
		std::string sources_text (sources_val->as_string ().c_str ());
		if (!ec && decode_unsigned (sources_text, sources))
		{
			ec = nano::error_rpc::invalid_sources;
		}
	}
	if (auto* destinations_val = request.if_contains ("destinations"))
	{
		std::string destinations_text (destinations_val->as_string ().c_str ());
		if (!ec && decode_unsigned (destinations_text, destinations))
		{
			ec = nano::error_rpc::invalid_destinations;
		}
	}
	auto hash (hash_impl ());
	if (!ec)
	{
		boost::json::array blocks;
		auto transaction = node.ledger.tx_begin_read ();
		auto block = node.ledger.any.block_get (transaction, hash);
		if (block != nullptr)
		{
			std::deque<std::shared_ptr<nano::block>> republish_bundle;
			for (auto i (0); !hash.is_zero () && i < count; ++i)
			{
				block = node.ledger.any.block_get (transaction, hash);
				if (sources != 0) // Republish source chain
				{
					nano::block_hash source = block->source_field ().value_or (block->link_field ().value_or (0).as_block_hash ());
					auto block_a = node.ledger.any.block_get (transaction, source);
					std::vector<nano::block_hash> hashes;
					while (block_a != nullptr && hashes.size () < sources)
					{
						hashes.push_back (source);
						source = block_a->previous ();
						block_a = node.ledger.any.block_get (transaction, source);
					}
					std::reverse (hashes.begin (), hashes.end ());
					for (auto & hash_l : hashes)
					{
						block_a = node.ledger.any.block_get (transaction, hash_l);
						republish_bundle.push_back (std::move (block_a));
						blocks.push_back (boost::json::value (hash_l.to_string ()));
					}
				}
				republish_bundle.push_back (std::move (block)); // Republish block
				blocks.push_back (boost::json::value (hash.to_string ()));
				if (destinations != 0) // Republish destination chain
				{
					auto block_b = node.ledger.any.block_get (transaction, hash);
					auto destination = block_b->is_send () ? block_b->destination () : nano::account (0);
					if (!destination.is_zero ())
					{
						if (!node.ledger.any.pending_get (transaction, nano::pending_key{ destination, hash }))
						{
							nano::block_hash previous (node.ledger.any.account_head (transaction, destination));
							auto block_d = node.ledger.any.block_get (transaction, previous);
							nano::block_hash source;
							std::vector<nano::block_hash> hashes;
							while (block_d != nullptr && hash != source)
							{
								hashes.push_back (previous);
								source = block_d->source_field ().value_or (block_d->is_send () ? 0 : block_d->link_field ().value_or (0).as_block_hash ());
								previous = block_d->previous ();
								block_d = node.ledger.any.block_get (transaction, previous);
							}
							std::reverse (hashes.begin (), hashes.end ());
							if (hashes.size () > destinations)
							{
								hashes.resize (destinations);
							}
							for (auto & hash_l : hashes)
							{
								block_d = node.ledger.any.block_get (transaction, hash_l);
								republish_bundle.push_back (std::move (block_d));
								blocks.push_back (boost::json::value (hash_l.to_string ()));
							}
						}
					}
				}
				hash = node.ledger.any.block_successor (transaction, hash).value_or (0);
			}
			node.network.flood_block_many (std::move (republish_bundle), nano::transport::traffic_type::block_broadcast_rpc, 25ms);
			response_l["success"] = ""; // obsolete
			response_l["blocks"] = std::move (blocks);
		}
		else
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void nano::json_handler::search_pending ()
{
	response_l["deprecated"] = "1";
	search_receivable ();
}

void nano::json_handler::search_receivable ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto error (wallet->search_receivable ());
		response_l["started"] = !error;
	}
	response_errors ();
}

void nano::json_handler::search_pending_all ()
{
	response_l["deprecated"] = "1";
	search_receivable_all ();
}

void nano::json_handler::search_receivable_all ()
{
	if (!ec)
	{
		node.wallets.search_receivable_all ();
		response_l["success"] = "";
	}
	response_errors ();
}

void nano::json_handler::send ()
{
	auto wallet (wallet_impl ());
	auto amount (amount_impl ());
	// Sending 0 amount is invalid with state blocks
	if (!ec && amount.is_zero ())
	{
		ec = nano::error_common::invalid_amount;
	}
	std::string source_text (request.at ("source").as_string ().c_str ());
	auto source (account_impl (source_text, nano::error_rpc::bad_source));
	std::string destination_text (request.at ("destination").as_string ().c_str ());
	auto destination (account_impl (destination_text, nano::error_rpc::bad_destination));
	if (!ec)
	{
		auto work (work_optional_impl ());
		nano::uint128_t balance (0);
		if (!ec && work == 0 && !node.work_generation_enabled ())
		{
			ec = nano::error_common::disabled_work_generation;
		}
		if (!ec)
		{
			wallet_locked_impl (wallet);
			wallet_account_impl (wallet, source);
			auto block_transaction = node.ledger.tx_begin_read ();
			auto info (account_info_impl (block_transaction, source));
			if (!ec)
			{
				balance = (info.balance).number ();
			}
			if (!ec && work)
			{
				nano::block_details details (info.epoch (), true, false, false);
				if (node.network_params.work.difficulty (nano::work_version::work_1, info.head, work) < node.network_params.work.threshold (nano::work_version::work_1, details))
				{
					ec = nano::error_common::invalid_work;
				}
			}
		}
		if (!ec)
		{
			bool generate_work (work == 0); // Disable work generation if "work" option is provided
			boost::optional<std::string> send_id;
			if (auto* send_id_val = request.if_contains ("id"))
			{
				send_id = std::string (send_id_val->as_string ().c_str ());
			}
			auto response_a (response);
			auto response_data (std::make_shared<boost::json::object> (response_l));
			wallet->send_async (
			source, destination, amount.number (), [balance, amount, response_a, response_data] (std::shared_ptr<nano::block> const & block_a) {
				if (block_a != nullptr)
				{
					(*response_data)["block"] = block_a->hash ().to_string ();
					response_a (boost::json::serialize (*response_data));
				}
				else
				{
					if (balance >= amount.number ())
					{
						json_error_response (response_a, "Error generating block");
					}
					else
					{
						std::error_code ec (nano::error_common::insufficient_balance);
						json_error_response (response_a, ec.message ());
					}
				}
			},
			work, generate_work, send_id);
		}
	}
	// Because of send_async
	if (ec)
	{
		response_errors ();
	}
}

void nano::json_handler::sign ()
{
	bool const json_block_l = request.contains ("json_block") ? request.at ("json_block").as_bool () : false;
	// Retrieving hash
	nano::block_hash hash (0);
	if (request.contains ("hash"))
	{
		hash = hash_impl ();
	}
	// Retrieving block
	std::shared_ptr<nano::block> block;
	if (!ec && request.contains ("block"))
	{
		block = block_impl (true);
		if (block != nullptr)
		{
			hash = block->hash ();
		}
	}

	// Hash or block are not initialized
	if (!ec && hash.is_zero ())
	{
		ec = nano::error_blocks::invalid_block;
	}
	// Hash is initialized without config permission
	else if (!ec && !hash.is_zero () && block == nullptr && !node_rpc_config.enable_sign_hash)
	{
		ec = nano::error_rpc::sign_hash_disabled;
	}
	if (!ec)
	{
		nano::raw_key prv;
		prv.clear ();
		// Retrieving private key from request
		if (auto* key_val = request.if_contains ("key"))
		{
			std::string key_text (key_val->as_string ().c_str ());
			if (prv.decode_hex (key_text))
			{
				ec = nano::error_common::bad_private_key;
			}
		}
		else
		{
			// Retrieving private key from wallet
			bool has_account = request.contains ("account");
			bool has_wallet = request.contains ("wallet");
			if (has_wallet && has_account)
			{
				auto account (account_impl ());
				auto wallet (wallet_impl ());
				if (!ec)
				{
					wallet_locked_impl (wallet);
					wallet_account_impl (wallet, account);
					if (!ec)
					{
						wallet->fetch_prv (account, prv);
					}
				}
			}
		}
		// Signing
		if (prv != 0)
		{
			nano::public_key pub (nano::pub_key (prv));
			nano::signature signature (nano::sign_message (prv, pub, hash));
			response_l["signature"] = signature.to_string ();
			if (block != nullptr)
			{
				block->signature_set (signature);

				if (json_block_l)
				{
					boost::json::object block_node_l;
					block->serialize_json (block_node_l);
					response_l["block"] = std::move (block_node_l);
				}
				else
				{
					std::string contents;
					block->serialize_json (contents);
					response_l["block"] = contents;
				}
			}
		}
		else
		{
			ec = nano::error_rpc::block_create_key_required;
		}
	}
	response_errors ();
}

void nano::json_handler::stats ()
{
	std::string type (request.contains ("type") ? request.at ("type").as_string ().c_str () : "");

	auto respond_with_sink = [this] (auto & sink) {
		response_l = sink.to_object ();
		response_l["stat_duration_seconds"] = node.stats.last_reset ().count ();
	};

	if (type == "counters")
	{
		nano::stat_json_writer sink;
		node.stats.log_counters (sink);
		respond_with_sink (sink);
	}
	else if (type == "samples")
	{
		nano::stat_json_writer sink;
		node.stats.log_samples (sink);
		respond_with_sink (sink);
	}
	else if (type == "objects")
	{
		construct_json (node.container_info ().to_legacy ("node").get (), response_l);
	}
	else if (type == "database")
	{
		node.store.backend.collect_memory_stats (response_l);
	}
	else
	{
		ec = nano::error_rpc::invalid_missing_type;
	}

	response_errors ();
}

void nano::json_handler::stats_clear ()
{
	node.stats.clear ();
	response_l["success"] = "";
	response (boost::json::serialize (response_l));
}

void nano::json_handler::stop ()
{
	response_l["success"] = "";
	response_errors ();
	if (!ec)
	{
		try
		{
			stop_callback ();
		}
		catch (std::exception const & ex)
		{
			release_assert (false, "unexpected exception in stop callback", ex.what ());
		}
	}
}

void nano::json_handler::telemetry ()
{
	std::optional<std::string> address_text;
	std::optional<std::string> port_text;
	if (auto* address_val = request.if_contains ("address"))
	{
		address_text = std::string (address_val->as_string ().c_str ());
	}
	if (auto* port_val = request.if_contains ("port"))
	{
		port_text = std::string (port_val->as_string ().c_str ());
	}

	if (address_text || port_text)
	{
		// Check both are specified
		nano::endpoint endpoint{};
		if (address_text && port_text)
		{
			uint16_t port;
			if (!nano::parse_port (*port_text, port))
			{
				boost::asio::ip::address address;
				if (!nano::parse_address (*address_text, address))
				{
					endpoint = { address, port };

					if (address.is_loopback () && port == node.network.endpoint ().port ())
					{
						// Requesting telemetry metrics locally
						auto telemetry_data = node.local_telemetry ();
						auto const should_ignore_identification_metrics = false;
						telemetry_data.serialize_json (response_l, should_ignore_identification_metrics);

						response_errors ();
						return;
					}
				}
				else
				{
					ec = nano::error_common::invalid_ip_address;
				}
			}
			else
			{
				ec = nano::error_common::invalid_port;
			}
		}
		else
		{
			ec = nano::error_rpc::requires_port_and_address;
		}

		if (!ec)
		{
			auto maybe_telemetry = node.telemetry.get_telemetry (nano::transport::map_endpoint_to_v6 (endpoint));
			if (maybe_telemetry)
			{
				auto telemetry = *maybe_telemetry;
				auto const should_ignore_identification_metrics = false;
				telemetry.serialize_json (response_l, should_ignore_identification_metrics);
			}
			else
			{
				ec = nano::error_rpc::peer_not_found;
			}

			response_errors ();
		}
		else
		{
			response_errors ();
		}
	}
	else
	{
		// By default, consolidated (average or mode) telemetry metrics are returned,
		// setting "raw" to true returns metrics from all nodes requested.
		bool output_raw = false;
		if (auto* raw_val = request.if_contains ("raw"))
		{
			output_raw = raw_val->as_bool ();
		}

		auto telemetry_responses = node.telemetry.get_all_telemetries ();
		if (output_raw)
		{
			boost::json::array metrics;
			for (auto & telemetry_metrics : telemetry_responses)
			{
				boost::json::object metric;
				auto const should_ignore_identification_metrics = false;
				telemetry_metrics.second.serialize_json (metric, should_ignore_identification_metrics);
				metric["address"] = telemetry_metrics.first.address ().to_string ();
				metric["port"] = telemetry_metrics.first.port ();
				metrics.push_back (std::move (metric));
			}

			response_l["metrics"] = std::move (metrics);
		}
		else
		{
			// Default case without any parameters, requesting telemetry metrics locally
			auto telemetry_data = node.local_telemetry ();
			auto const should_ignore_identification_metrics = false;
			telemetry_data.serialize_json (response_l, should_ignore_identification_metrics);

			response_errors ();
			return;
		}

		response_errors ();
	}
}

void nano::json_handler::unchecked ()
{
	bool const json_block_l = request.contains ("json_block") ? request.at ("json_block").as_bool () : false;
	auto count (count_optional_impl ());
	if (!ec)
	{
		boost::json::object unchecked;
		node.unchecked.for_each (
		[&unchecked, &json_block_l] (nano::unchecked_key const & key, nano::unchecked_info const & info) {
			if (json_block_l)
			{
				boost::json::object block_node_l;
				info.block->serialize_json (block_node_l);
				unchecked[info.block->hash ().to_string ()] = std::move (block_node_l);
			}
			else
			{
				std::string contents;
				info.block->serialize_json (contents);
				unchecked[info.block->hash ().to_string ()] = contents;
			} }, [iterations = 0, count = count] () mutable { return iterations++ < count; });
		response_l["blocks"] = std::move (unchecked);
	}
	response_errors ();
}

void nano::json_handler::unchecked_clear ()
{
	node.workers.post (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		rpc_l->node.unchecked.clear ();
		rpc_l->response_l["success"] = "";
		rpc_l->response_errors ();
	}));
}

void nano::json_handler::unchecked_get ()
{
	bool const json_block_l = request.contains ("json_block") ? request.at ("json_block").as_bool () : false;
	auto hash (hash_impl ());
	if (!ec)
	{
		bool done = false;
		node.unchecked.for_each (
		[&] (nano::unchecked_key const & key, nano::unchecked_info const & info) {
			if (key.hash == hash)
			{
				response_l["modified_timestamp"] = std::to_string (info.modified ());

				if (json_block_l)
				{
					boost::json::object block_node_l;
					info.block->serialize_json (block_node_l);
					response_l["contents"] = std::move (block_node_l);
				}
				else
				{
					std::string contents;
					info.block->serialize_json (contents);
					response_l["contents"] = contents;
				}
				done = true;
			} }, [&] () { return !done; });
		if (response_l.empty ())
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void nano::json_handler::unchecked_keys ()
{
	bool const json_block_l = request.contains ("json_block") ? request.at ("json_block").as_bool () : false;
	auto count (count_optional_impl ());
	nano::block_hash key (0);
	if (auto* key_val = request.if_contains ("key"))
	{
		std::string hash_text (key_val->as_string ().c_str ());
		if (!ec && key.decode_hex (hash_text))
		{
			ec = nano::error_rpc::bad_key;
		}
	}
	if (!ec)
	{
		boost::json::array unchecked;
		node.unchecked.for_each (
		key,
		[&unchecked, json_block_l] (nano::unchecked_key const & key, nano::unchecked_info const & info) {
			boost::json::object entry;
			entry["key"] = key.key ().to_string ();
			entry["hash"] = info.block->hash ().to_string ();
			entry["modified_timestamp"] = std::to_string (info.modified ());
			if (json_block_l)
			{
				boost::json::object block_node_l;
				info.block->serialize_json (block_node_l);
				entry["contents"] = std::move (block_node_l);
			}
			else
			{
				std::string contents;
				info.block->serialize_json (contents);
				entry["contents"] = contents;
			}
			unchecked.push_back (std::move (entry)); }, [&unchecked, &count] () { return unchecked.size () < count; });
		response_l["unchecked"] = std::move (unchecked);
	}
	response_errors ();
}

void nano::json_handler::unopened ()
{
	auto count{ count_optional_impl () };
	auto threshold{ threshold_optional_impl () };
	nano::account start{ 1 }; // exclude burn account by default
	if (auto* account_val = request.if_contains ("account"))
	{
		std::string account_text (account_val->as_string ().c_str ());
		start = account_impl (account_text);
	}
	if (!ec)
	{
		auto transaction = node.store.tx_begin_read ();
		auto iterator = node.store.pending.begin (transaction, nano::pending_key (start, 0));
		auto end = node.store.pending.end (transaction);
		nano::account current_account = start;
		nano::uint128_t current_account_sum{ 0 };
		boost::json::object accounts;
		while (iterator != end && accounts.size () < count)
		{
			nano::pending_key key{ iterator->first };
			nano::account account{ key.account };
			nano::pending_info info{ iterator->second };
			if (node.store.account.exists (transaction, account))
			{
				if (account.number () == std::numeric_limits<nano::uint256_t>::max ())
				{
					break;
				}
				// Skip existing accounts
				iterator = node.store.pending.begin (transaction, nano::pending_key (inc_sat (account.number ()), 0));
			}
			else
			{
				if (account != current_account)
				{
					if (current_account_sum > 0)
					{
						if (current_account_sum >= threshold.number ())
						{
							accounts[current_account.to_account ()] = current_account_sum.convert_to<std::string> ();
						}
						current_account_sum = 0;
					}
					current_account = account;
				}
				current_account_sum += info.amount.number ();
				++iterator;
			}
		}
		// last one after iterator reaches end
		if (accounts.size () < count && current_account_sum > 0 && current_account_sum >= threshold.number ())
		{
			accounts[current_account.to_account ()] = current_account_sum.convert_to<std::string> ();
		}
		response_l["accounts"] = std::move (accounts);
	}
	response_errors ();
}

void nano::json_handler::uptime ()
{
	response_l["seconds"] = std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - node.startup_time).count ();
	response_errors ();
}

void nano::json_handler::version ()
{
	response_l["rpc_version"] = "1";
	response_l["store_version"] = std::to_string (node.store_version ());
	response_l["protocol_version"] = std::to_string (node.network_params.network.protocol_version);
	response_l["node_vendor"] = boost::str (boost::format ("Nano %1%") % NANO_VERSION_STRING);
	response_l["store_vendor"] = node.store.vendor_get ();
	response_l["network"] = node.network_params.network.get_current_network_as_string ();
	response_l["network_identifier"] = node.network_params.ledger.genesis->hash ().to_string ();
	response_l["build_info"] = BUILD_INFO;
	response_errors ();
}

void nano::json_handler::validate_account_number ()
{
	auto account (account_impl ());
	(void)account;
	response_l["valid"] = ec ? "0" : "1";
	ec = std::error_code (); // error is just invalid account
	response_errors ();
}

void nano::json_handler::wallet_add ()
{
	node.workers.post (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		auto wallet (rpc_l->wallet_impl ());
		if (!rpc_l->ec)
		{
			std::string key_text (rpc_l->request.at ("key").as_string ().c_str ());
			nano::raw_key key;
			if (!key.decode_hex (key_text))
			{
				bool const generate_work = rpc_l->request.contains ("work") ? rpc_l->request.at ("work").as_bool () : true;
				auto pub (wallet->insert_adhoc (key, generate_work));
				if (!pub.is_zero ())
				{
					rpc_l->response_l["account"] = pub.to_account ();
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

void nano::json_handler::wallet_add_watch ()
{
	node.workers.post (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		auto wallet (rpc_l->wallet_impl ());
		if (!rpc_l->ec)
		{
			if (!wallet->is_locked ())
			{
				for (auto const & account_val : rpc_l->request.at ("accounts").as_array ())
				{
					auto account (rpc_l->account_impl (std::string (account_val.as_string ().c_str ())));
					if (!rpc_l->ec)
					{
						if (wallet->insert_watch (account))
						{
							rpc_l->ec = nano::error_common::bad_public_key;
						}
					}
				}
				if (!rpc_l->ec)
				{
					rpc_l->response_l["success"] = "";
				}
			}
			else
			{
				rpc_l->ec = nano::error_common::wallet_locked;
			}
		}
		rpc_l->response_errors ();
	}));
}

void nano::json_handler::wallet_info ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		nano::uint128_t balance (0);
		nano::uint128_t receivable (0);
		uint64_t count (0);
		uint64_t block_count (0);
		uint64_t cemented_block_count (0);
		uint64_t deterministic_count (0);
		uint64_t adhoc_count (0);
		auto block_transaction = node.ledger.tx_begin_read ();
		for (auto const & account : wallet->accounts ())
		{
			auto account_info = node.ledger.any.account_get (block_transaction, account);
			if (account_info)
			{
				block_count += account_info->block_count;
				balance += account_info->balance.number ();
			}

			nano::confirmation_height_info confirmation_info{};
			if (!node.store.confirmation_height.get (block_transaction, account, confirmation_info))
			{
				cemented_block_count += confirmation_info.height;
			}

			receivable += node.ledger.account_receivable (block_transaction, account);

			nano::key_type key_type_l (wallet->key_type (account));
			if (key_type_l == nano::key_type::deterministic)
			{
				deterministic_count++;
			}
			else if (key_type_l == nano::key_type::adhoc)
			{
				adhoc_count++;
			}

			++count;
		}

		uint32_t deterministic_index (wallet->get_deterministic_index ());
		response_l["balance"] = balance.convert_to<std::string> ();
		response_l["pending"] = receivable.convert_to<std::string> ();
		response_l["receivable"] = receivable.convert_to<std::string> ();
		response_l["accounts_count"] = std::to_string (count);
		response_l["accounts_block_count"] = std::to_string (block_count);
		response_l["accounts_cemented_block_count"] = std::to_string (cemented_block_count);
		response_l["deterministic_count"] = std::to_string (deterministic_count);
		response_l["adhoc_count"] = std::to_string (adhoc_count);
		response_l["deterministic_index"] = std::to_string (deterministic_index);
	}

	response_errors ();
}

void nano::json_handler::wallet_balances ()
{
	auto wallet (wallet_impl ());
	auto threshold (threshold_optional_impl ());
	if (!ec)
	{
		boost::json::object balances;
		auto block_transaction = node.ledger.tx_begin_read ();
		for (auto const & account : wallet->accounts ())
		{
			nano::uint128_t balance = node.ledger.any.account_balance (block_transaction, account).value_or (0).number ();
			if (balance >= threshold.number ())
			{
				boost::json::object entry;
				nano::uint128_t receivable = node.ledger.account_receivable (block_transaction, account);
				entry["balance"] = balance.convert_to<std::string> ();
				entry["pending"] = receivable.convert_to<std::string> ();
				entry["receivable"] = receivable.convert_to<std::string> ();
				balances[account.to_account ()] = std::move (entry);
			}
		}
		response_l["balances"] = std::move (balances);
	}
	response_errors ();
}

void nano::json_handler::wallet_change_seed ()
{
	node.workers.post (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		auto wallet (rpc_l->wallet_impl ());
		if (!rpc_l->ec)
		{
			std::string seed_text (rpc_l->request.at ("seed").as_string ().c_str ());
			nano::raw_key seed;
			if (!seed.decode_hex (seed_text))
			{
				auto count (static_cast<uint32_t> (rpc_l->count_optional_impl (0)));
				if (!wallet->is_locked ())
				{
					nano::public_key account (wallet->change_seed (seed, count));
					rpc_l->response_l["success"] = "";
					rpc_l->response_l["last_restored_account"] = account.to_account ();
					auto index (wallet->get_deterministic_index ());
					debug_assert (index > 0);
					rpc_l->response_l["restored_count"] = std::to_string (index);
				}
				else
				{
					rpc_l->ec = nano::error_common::wallet_locked;
				}
			}
			else
			{
				rpc_l->ec = nano::error_common::bad_seed;
			}
		}
		rpc_l->response_errors ();
	}));
}

void nano::json_handler::wallet_contains ()
{
	auto account (account_impl ());
	auto wallet (wallet_impl ());
	if (!ec)
	{
		response_l["exists"] = wallet->exists (account) ? "1" : "0";
	}
	response_errors ();
}

void nano::json_handler::wallet_create ()
{
	node.workers.post (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		nano::raw_key seed;
		bool has_seed = false;
		if (auto* seed_val = rpc_l->request.if_contains ("seed"))
		{
			std::string seed_text (seed_val->as_string ().c_str ());
			has_seed = true;
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
				rpc_l->response_l["wallet"] = wallet_id.to_string ();
			}
			else
			{
				rpc_l->ec = nano::error_common::wallet_lmdb_max_dbs;
			}
			if (!rpc_l->ec && has_seed)
			{
				nano::public_key account (wallet->change_seed (seed));
				rpc_l->response_l["last_restored_account"] = account.to_account ();
				auto index (wallet->get_deterministic_index ());
				debug_assert (index > 0);
				rpc_l->response_l["restored_count"] = std::to_string (index);
			}
		}
		rpc_l->response_errors ();
	}));
}

void nano::json_handler::wallet_destroy ()
{
	node.workers.post (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		std::string wallet_text (rpc_l->request.at ("wallet").as_string ().c_str ());
		nano::wallet_id wallet;
		if (!wallet.decode_hex (wallet_text))
		{
			auto existing (rpc_l->node.wallets.items.find (wallet));
			if (existing != rpc_l->node.wallets.items.end ())
			{
				rpc_l->node.wallets.destroy (wallet);
				bool destroyed (rpc_l->node.wallets.items.find (wallet) == rpc_l->node.wallets.items.end ());
				rpc_l->response_l["destroyed"] = destroyed ? "1" : "0";
			}
			else
			{
				rpc_l->ec = nano::error_common::wallet_not_found;
			}
		}
		else
		{
			rpc_l->ec = nano::error_common::bad_wallet_number;
		}
		rpc_l->response_errors ();
	}));
}

void nano::json_handler::wallet_export ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		std::string json;
		wallet->serialize_json (json);
		response_l["json"] = json;
	}
	response_errors ();
}

void nano::json_handler::wallet_frontiers ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		boost::json::object frontiers;
		auto block_transaction = node.ledger.tx_begin_read ();
		for (auto const & account : wallet->accounts ())
		{
			auto latest (node.ledger.any.account_head (block_transaction, account));
			if (!latest.is_zero ())
			{
				frontiers[account.to_account ()] = latest.to_string ();
			}
		}
		response_l["frontiers"] = std::move (frontiers);
	}
	response_errors ();
}

void nano::json_handler::wallet_history ()
{
	uint64_t modified_since (0);
	if (auto* modified_since_val = request.if_contains ("modified_since"))
	{
		std::string modified_since_text (modified_since_val->as_string ().c_str ());
		if (decode_unsigned (modified_since_text, modified_since))
		{
			ec = nano::error_rpc::invalid_timestamp;
		}
	}
	auto wallet (wallet_impl ());
	if (!ec)
	{
		std::multimap<uint64_t, boost::json::object, std::greater<uint64_t>> entries;
		auto block_transaction = node.ledger.tx_begin_read ();
		for (auto const & account : wallet->accounts ())
		{
			auto info = node.ledger.any.account_get (block_transaction, account);
			if (info)
			{
				auto timestamp (info->modified);
				auto hash (info->head);
				while (timestamp >= modified_since && !hash.is_zero ())
				{
					auto block = node.ledger.any.block_get (block_transaction, hash);
					timestamp = block->sideband ().timestamp;
					if (block != nullptr && timestamp >= modified_since)
					{
						boost::json::object entry;
						std::vector<nano::public_key> no_filter;
						history_visitor visitor (*this, false, block_transaction, entry, hash, no_filter);
						block->visit (visitor);
						if (!entry.empty ())
						{
							entry["block_account"] = account.to_account ();
							entry["hash"] = hash.to_string ();
							entry["local_timestamp"] = std::to_string (timestamp);
							entries.insert (std::make_pair (timestamp, entry));
						}
						hash = block->previous ();
					}
					else
					{
						hash.clear ();
					}
				}
			}
		}
		boost::json::array history;
		for (auto i (entries.begin ()), n (entries.end ()); i != n; ++i)
		{
			history.push_back (i->second);
		}
		response_l["history"] = std::move (history);
	}
	response_errors ();
}

void nano::json_handler::wallet_key_valid ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto valid (!wallet->is_locked ());
		response_l["valid"] = valid ? "1" : "0";
	}
	response_errors ();
}

void nano::json_handler::wallet_ledger ()
{
	bool const representative = request.contains ("representative") ? request.at ("representative").as_bool () : false;
	bool const weight = request.contains ("weight") ? request.at ("weight").as_bool () : false;
	bool const pending = request.contains ("pending") ? request.at ("pending").as_bool () : false;
	bool const receivable = request.contains ("receivable") ? request.at ("receivable").as_bool () : pending;
	uint64_t modified_since (0);
	if (auto* modified_since_val = request.if_contains ("modified_since"))
	{
		std::string modified_since_text (modified_since_val->as_string ().c_str ());
		modified_since = strtoul (modified_since_text.c_str (), NULL, 10);
	}
	auto wallet (wallet_impl ());
	if (!ec)
	{
		boost::json::object accounts;
		auto block_transaction = node.ledger.tx_begin_read ();
		for (auto const & account : wallet->accounts ())
		{
			auto info = node.ledger.any.account_get (block_transaction, account);
			if (info)
			{
				if (info->modified >= modified_since)
				{
					boost::json::object entry;
					entry["frontier"] = info->head.to_string ();
					entry["open_block"] = info->open_block.to_string ();
					entry["representative_block"] = node.ledger.representative_block (block_transaction, info->head).to_string ();
					std::string balance = nano::uint128_union (info->balance).to_string_dec ();
					entry["balance"] = balance;
					entry["modified_timestamp"] = std::to_string (info->modified);
					entry["block_count"] = std::to_string (info->block_count);
					if (representative)
					{
						entry["representative"] = info->representative.to_account ();
					}
					if (weight)
					{
						auto account_weight (node.ledger.weight_exact (block_transaction, account));
						entry["weight"] = account_weight.convert_to<std::string> ();
					}
					if (receivable)
					{
						auto account_receivable (node.ledger.account_receivable (block_transaction, account));
						entry["pending"] = account_receivable.convert_to<std::string> ();
						entry["receivable"] = account_receivable.convert_to<std::string> ();
					}
					accounts[account.to_account ()] = std::move (entry);
				}
			}
		}
		response_l["accounts"] = std::move (accounts);
	}
	response_errors ();
}

void nano::json_handler::wallet_lock ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		nano::raw_key empty;
		empty.clear ();
		wallet->store.password.value_set (empty);
		response_l["locked"] = "1";

		node.logger.warn (nano::log::type::rpc, "Wallet locked");
	}
	response_errors ();
}

void nano::json_handler::wallet_pending ()
{
	response_l["deprecated"] = "1";
	wallet_receivable ();
}

void nano::json_handler::wallet_receivable ()
{
	auto wallet (wallet_impl ());
	auto count (count_optional_impl ());
	auto threshold (threshold_optional_impl ());
	bool const source = request.contains ("source") ? request.at ("source").as_bool () : false;
	bool const min_version = request.contains ("min_version") ? request.at ("min_version").as_bool () : false;
	bool const include_active = request.contains ("include_active") ? request.at ("include_active").as_bool () : false;
	bool const include_only_confirmed = request.contains ("include_only_confirmed") ? request.at ("include_only_confirmed").as_bool () : true;
	auto simple (threshold.is_zero () && !source && !min_version);
	if (!ec)
	{
		boost::json::object pending;
		auto block_transaction = node.ledger.tx_begin_read ();
		for (auto const & account : wallet->accounts ())
		{
			if (simple)
			{
				boost::json::array peers_l;
				for (auto ii (node.store.pending.begin (block_transaction, nano::pending_key (account, 0))), nn (node.store.pending.end (block_transaction)); ii != nn && nano::pending_key (ii->first).account == account && peers_l.size () < count; ++ii)
				{
					nano::pending_key key (ii->first);
					if (block_confirmed (node, block_transaction, key.hash, include_active, include_only_confirmed))
					{
						peers_l.push_back (boost::json::value (key.hash.to_string ()));
					}
				}
				if (!peers_l.empty ())
				{
					pending[account.to_account ()] = std::move (peers_l);
				}
			}
			else
			{
				boost::json::object peers_l;
				for (auto ii (node.store.pending.begin (block_transaction, nano::pending_key (account, 0))), nn (node.store.pending.end (block_transaction)); ii != nn && nano::pending_key (ii->first).account == account && peers_l.size () < count; ++ii)
				{
					nano::pending_key key (ii->first);
					if (block_confirmed (node, block_transaction, key.hash, include_active, include_only_confirmed))
					{
						nano::pending_info info (ii->second);
						if (info.amount.number () >= threshold.number ())
						{
							if (source || min_version)
							{
								boost::json::object pending_tree;
								pending_tree["amount"] = info.amount.number ().convert_to<std::string> ();
								if (source)
								{
									pending_tree["source"] = info.source.to_account ();
								}
								if (min_version)
								{
									pending_tree["min_version"] = epoch_as_string (info.epoch);
								}
								peers_l[key.hash.to_string ()] = std::move (pending_tree);
							}
							else
							{
								peers_l[key.hash.to_string ()] = info.amount.number ().convert_to<std::string> ();
							}
						}
					}
				}
				if (!peers_l.empty ())
				{
					pending[account.to_account ()] = std::move (peers_l);
				}
			}
		}
		response_l["blocks"] = std::move (pending);
	}
	response_errors ();
}

void nano::json_handler::wallet_representative ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		response_l["representative"] = wallet->get_representative ().to_account ();
	}
	response_errors ();
}

void nano::json_handler::wallet_representative_set ()
{
	node.workers.post (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		auto wallet (rpc_l->wallet_impl ());
		std::string representative_text (rpc_l->request.at ("representative").as_string ().c_str ());
		auto representative (rpc_l->account_impl (representative_text, nano::error_rpc::bad_representative_number));
		if (!rpc_l->ec)
		{
			bool update_existing_accounts (rpc_l->request.contains ("update_existing_accounts") ? rpc_l->request.at ("update_existing_accounts").as_bool () : false);
			if (update_existing_accounts && wallet->is_locked ())
			{
				rpc_l->ec = nano::error_common::wallet_locked;
			}
			else
			{
				wallet->set_representative (representative);
				rpc_l->response_l["set"] = "1";
			}
			// Change representative for all wallet accounts
			if (!rpc_l->ec && update_existing_accounts)
			{
				std::vector<nano::account> accounts;
				{
					auto block_transaction = rpc_l->node.ledger.tx_begin_read ();
					for (auto const & account : wallet->accounts ())
					{
						auto info = rpc_l->node.ledger.any.account_get (block_transaction, account);
						if (info)
						{
							if (info->representative != representative)
							{
								accounts.push_back (account);
							}
						}
					}
				}
				for (auto & account : accounts)
				{
					wallet->change_async (
					account, representative, [] (std::shared_ptr<nano::block> const &) {}, 0, false);
				}
			}
		}
		rpc_l->response_errors ();
	}));
}

void nano::json_handler::wallet_republish ()
{
	auto wallet (wallet_impl ());
	auto count (count_impl ());
	if (!ec)
	{
		boost::json::array blocks;
		std::deque<std::shared_ptr<nano::block>> republish_bundle;
		auto block_transaction = node.ledger.tx_begin_read ();
		for (auto const & account : wallet->accounts ())
		{
			auto latest (node.ledger.any.account_head (block_transaction, account));
			std::shared_ptr<nano::block> block;
			std::vector<nano::block_hash> hashes;
			while (!latest.is_zero () && hashes.size () < count)
			{
				hashes.push_back (latest);
				block = node.ledger.any.block_get (block_transaction, latest);
				if (block != nullptr)
				{
					latest = block->previous ();
				}
				else
				{
					latest.clear ();
				}
			}
			std::reverse (hashes.begin (), hashes.end ());
			for (auto & hash : hashes)
			{
				block = node.ledger.any.block_get (block_transaction, hash);
				republish_bundle.push_back (std::move (block));
				blocks.push_back (boost::json::value (hash.to_string ()));
			}
		}
		node.network.flood_block_many (std::move (republish_bundle), nano::transport::traffic_type::keepalive, 25ms);
		response_l["blocks"] = std::move (blocks);
	}
	response_errors ();
}

void nano::json_handler::wallet_seed ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		nano::raw_key seed;
		if (!wallet->get_seed (seed))
		{
			response_l["seed"] = seed.to_string ();
		}
		else
		{
			ec = nano::error_common::wallet_locked;
		}
	}
	response_errors ();
}

void nano::json_handler::wallet_work_get ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		boost::json::object works;
		for (auto const & account : wallet->accounts ())
		{
			uint64_t work (0);
			auto error_work (wallet->get_work (account, work));
			(void)error_work;
			works[account.to_account ()] = nano::to_string_hex (work);
		}
		response_l["works"] = std::move (works);
	}
	response_errors ();
}

void nano::json_handler::work_generate ()
{
	std::optional<nano::account> account;
	// Default to work_1 if not specified
	auto work_version (work_version_optional_impl (nano::work_version::work_1));
	if (auto* account_val = request.if_contains ("account"))
	{
		std::string account_text (account_val->as_string ().c_str ());
		if (!ec)
		{
			account = account_impl (account_text);
		}
	}
	if (!ec)
	{
		auto hash (hash_impl ());
		auto difficulty (difficulty_optional_impl (work_version));
		multiplier_optional_impl (work_version, difficulty);
		if (!ec && (difficulty > node.max_work_generate_difficulty (work_version) || difficulty < node.network_params.work.threshold_entry (work_version, nano::block_type::state)))
		{
			ec = nano::error_rpc::difficulty_limit;
		}
		// Retrieving optional block
		std::shared_ptr<nano::block> block;
		if (!ec && request.contains ("block"))
		{
			block = block_impl (true);
			if (block != nullptr)
			{
				if (hash != block->root ().as_block_hash ())
				{
					ec = nano::error_rpc::block_root_mismatch;
				}
				if (!request.contains ("version"))
				{
					work_version = block->work_version ();
				}
				else if (!ec && work_version != block->work_version ())
				{
					ec = nano::error_rpc::block_work_version_mismatch;
				}
				// Difficulty calculation
				if (!ec && !request.contains ("difficulty") && !request.contains ("multiplier"))
				{
					difficulty = difficulty_ledger (*block);
				}
				// If optional block difficulty is higher than requested difficulty, send error
				if (!ec && node.network_params.work.difficulty (*block) >= difficulty)
				{
					ec = nano::error_rpc::block_work_enough;
				}
			}
		}
		if (!ec && response_l.empty ())
		{
			auto use_peers (request.contains ("use_peers") ? request.at ("use_peers").as_bool () : false);
			auto rpc_l (shared_from_this ());
			auto callback = [rpc_l, hash, work_version, this] (std::optional<uint64_t> const & work_a) {
				if (work_a)
				{
					boost::json::object response_l;
					response_l["hash"] = hash.to_string ();
					uint64_t work (work_a.value ());
					response_l["work"] = nano::to_string_hex (work);
					auto result_difficulty (rpc_l->node.network_params.work.difficulty (work_version, hash, work));
					response_l["difficulty"] = nano::to_string_hex (result_difficulty);
					auto result_multiplier = nano::difficulty::to_multiplier (result_difficulty, node.default_difficulty (work_version));
					response_l["multiplier"] = nano::to_string (result_multiplier);
					rpc_l->response (boost::json::serialize (response_l));
				}
				else
				{
					json_error_response (rpc_l->response, "Cancelled");
				}
			};
			if (!use_peers)
			{
				if (node.local_work_generation_enabled ())
				{
					auto error = node.distributed_work.make (work_version, hash, {}, difficulty, callback, {});
					if (error)
					{
						ec = nano::error_common::failure_work_generation;
					}
				}
				else
				{
					ec = nano::error_common::disabled_local_work_generation;
				}
			}
			else
			{
				if (!account.has_value ())
				{
					// Fetch account from block if not given
					auto transaction_l = node.ledger.tx_begin_read ();
					if (node.ledger.any.block_exists (transaction_l, hash))
					{
						account = node.ledger.any.block_account (transaction_l, hash).value ();
					}
				}
				auto secondary_work_peers_l (request.contains ("secondary_work_peers") ? request.at ("secondary_work_peers").as_bool () : false);
				auto const & peers_l (secondary_work_peers_l ? node.config.secondary_work_peers : node.config.work_peers);
				if (node.work_generation_enabled (peers_l))
				{
					node.work_generate (work_version, hash, difficulty, callback, account, secondary_work_peers_l);
				}
				else
				{
					ec = nano::error_common::disabled_work_generation;
				}
			}
		}
	}
	// Because of callback
	if (ec)
	{
		response_errors ();
	}
}

void nano::json_handler::work_cancel ()
{
	auto hash (hash_impl ());
	if (!ec)
	{
		node.observers.work_cancel.notify (hash);
		response_l["success"] = "";
	}
	response_errors ();
}

void nano::json_handler::work_get ()
{
	auto wallet (wallet_impl ());
	auto account (account_impl ());
	if (!ec)
	{
		if (!wallet->exists (account))
		{
			ec = nano::error_common::account_not_found_wallet;
		}
		else
		{
			uint64_t work (0);
			auto error_work (wallet->get_work (account, work));
			(void)error_work;
			response_l["work"] = nano::to_string_hex (work);
		}
	}
	response_errors ();
}

void nano::json_handler::work_set ()
{
	node.workers.post (create_worker_task ([] (std::shared_ptr<nano::json_handler> const & rpc_l) {
		auto wallet (rpc_l->wallet_impl ());
		auto account (rpc_l->account_impl ());
		auto work (rpc_l->work_optional_impl ());
		if (!rpc_l->ec)
		{
			if (!wallet->exists (account))
			{
				rpc_l->ec = nano::error_common::account_not_found_wallet;
			}
			else
			{
				wallet->set_work (account, work);
				rpc_l->response_l["success"] = "";
			}
		}
		rpc_l->response_errors ();
	}));
}

void nano::json_handler::work_validate ()
{
	auto hash (hash_impl ());
	auto work (work_optional_impl ());
	// Default to work_1 if not specified
	auto work_version (work_version_optional_impl (nano::work_version::work_1));
	auto difficulty (difficulty_optional_impl (work_version));
	multiplier_optional_impl (work_version, difficulty);
	if (!ec)
	{
		/* Transition to epoch_2 difficulty levels breaks previous behavior.
		 * When difficulty is not given, the default difficulty to validate changes when the first epoch_2 block is seen, breaking previous behavior.
		 * For this reason, when difficulty is not given, the "valid" field is no longer included in the response to break loudly any client expecting it.
		 * Instead, use the new fields:
		 * * valid_all: the work is valid at the current highest difficulty threshold
		 * * valid_receive: the work is valid for a receive block in an epoch_2 upgraded account
		 */

		auto result_difficulty (node.network_params.work.difficulty (work_version, hash, work));
		if (request.contains ("difficulty"))
		{
			response_l["valid"] = (result_difficulty >= difficulty) ? "1" : "0";
		}
		response_l["valid_all"] = (result_difficulty >= node.default_difficulty (work_version)) ? "1" : "0";
		response_l["valid_receive"] = (result_difficulty >= node.network_params.work.threshold (work_version, nano::block_details (nano::epoch::epoch_2, false, true, false))) ? "1" : "0";
		response_l["difficulty"] = nano::to_string_hex (result_difficulty);
		auto result_multiplier = nano::difficulty::to_multiplier (result_difficulty, node.default_difficulty (work_version));
		response_l["multiplier"] = nano::to_string (result_multiplier);
	}
	response_errors ();
}

void nano::json_handler::work_peer_add ()
{
	std::string address_text (request.at ("address").as_string ().c_str ());
	std::string port_text (request.at ("port").as_string ().c_str ());
	uint16_t port;
	if (!nano::parse_port (port_text, port))
	{
		node.config.work_peers.push_back (std::make_pair (address_text, port));
		response_l["success"] = "";
	}
	else
	{
		ec = nano::error_common::invalid_port;
	}
	response_errors ();
}

void nano::json_handler::work_peers ()
{
	boost::json::array work_peers_l;
	for (auto i (node.config.work_peers.begin ()), n (node.config.work_peers.end ()); i != n; ++i)
	{
		work_peers_l.push_back (boost::json::value (boost::str (boost::format ("%1%:%2%") % i->first % i->second)));
	}
	response_l["work_peers"] = std::move (work_peers_l);
	response_errors ();
}

void nano::json_handler::work_peers_clear ()
{
	node.config.work_peers.clear ();
	response_l["success"] = "";
	response_errors ();
}

void nano::json_handler::populate_backlog ()
{
	node.backlog_scan.trigger ();
	response_l["success"] = "";
	response_errors ();
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

void nano::inprocess_rpc_handler::process_request_v2 (rpc_handler_request_params const & params_a, std::string const & body_a, std::function<void (std::shared_ptr<std::string> const &)> response_a)
{
	std::string body_l = params_a.json_envelope (body_a);
	auto handler (std::make_shared<nano::ipc::flatbuffers_handler> (node, ipc_server, nullptr, node.config.ipc_config));
	handler->process_json (reinterpret_cast<uint8_t const *> (body_l.data ()), body_l.size (), response_a);
}

namespace
{
void construct_json (nano::container_info_component * component, boost::json::object & parent)
{
	// We are a leaf node, print name and exit
	if (!component->is_composite ())
	{
		auto & leaf_info = static_cast<nano::container_info_leaf *> (component)->get_info ();
		boost::json::object child;
		child["count"] = leaf_info.count;
		child["size"] = leaf_info.count * leaf_info.sizeof_element;
		parent[leaf_info.name] = std::move (child);
		return;
	}

	auto composite = static_cast<nano::container_info_composite *> (component);

	boost::json::object current;
	for (auto & child : composite->get_children ())
	{
		construct_json (child.get (), current);
	}

	parent[composite->get_name ()] = std::move (current);
}

// Any RPC handlers which require no arguments (excl default arguments) should go here.
// This is to prevent large if/else chains which compilers can have limits for (MSVC for instance has 128).
ipc_json_handler_no_arg_func_map create_ipc_json_handler_no_arg_func_map ()
{
	ipc_json_handler_no_arg_func_map no_arg_funcs;
	no_arg_funcs.emplace ("account_balance", &nano::json_handler::account_balance);
	no_arg_funcs.emplace ("account_block_count", &nano::json_handler::account_block_count);
	no_arg_funcs.emplace ("account_count", &nano::json_handler::account_count);
	no_arg_funcs.emplace ("account_create", &nano::json_handler::account_create);
	no_arg_funcs.emplace ("account_get", &nano::json_handler::account_get);
	no_arg_funcs.emplace ("account_history", &nano::json_handler::account_history);
	no_arg_funcs.emplace ("account_info", &nano::json_handler::account_info);
	no_arg_funcs.emplace ("account_key", &nano::json_handler::account_key);
	no_arg_funcs.emplace ("account_list", &nano::json_handler::account_list);
	no_arg_funcs.emplace ("account_move", &nano::json_handler::account_move);
	no_arg_funcs.emplace ("account_remove", &nano::json_handler::account_remove);
	no_arg_funcs.emplace ("account_representative", &nano::json_handler::account_representative);
	no_arg_funcs.emplace ("account_representative_set", &nano::json_handler::account_representative_set);
	no_arg_funcs.emplace ("account_weight", &nano::json_handler::account_weight);
	no_arg_funcs.emplace ("accounts_balances", &nano::json_handler::accounts_balances);
	no_arg_funcs.emplace ("accounts_representatives", &nano::json_handler::accounts_representatives);
	no_arg_funcs.emplace ("accounts_create", &nano::json_handler::accounts_create);
	no_arg_funcs.emplace ("accounts_frontiers", &nano::json_handler::accounts_frontiers);
	no_arg_funcs.emplace ("accounts_pending", &nano::json_handler::accounts_pending);
	no_arg_funcs.emplace ("accounts_receivable", &nano::json_handler::accounts_receivable);
	no_arg_funcs.emplace ("active_difficulty", &nano::json_handler::active_difficulty);
	no_arg_funcs.emplace ("available_supply", &nano::json_handler::available_supply);
	no_arg_funcs.emplace ("block_info", &nano::json_handler::block_info);
	no_arg_funcs.emplace ("block", &nano::json_handler::block_info);
	no_arg_funcs.emplace ("block_confirm", &nano::json_handler::block_confirm);
	no_arg_funcs.emplace ("blocks", &nano::json_handler::blocks);
	no_arg_funcs.emplace ("blocks_info", &nano::json_handler::blocks_info);
	no_arg_funcs.emplace ("block_account", &nano::json_handler::block_account);
	no_arg_funcs.emplace ("block_count", &nano::json_handler::block_count);
	no_arg_funcs.emplace ("block_create", &nano::json_handler::block_create);
	no_arg_funcs.emplace ("block_hash", &nano::json_handler::block_hash);
	no_arg_funcs.emplace ("bootstrap", &nano::json_handler::bootstrap);
	no_arg_funcs.emplace ("bootstrap_any", &nano::json_handler::bootstrap_any);
	no_arg_funcs.emplace ("bootstrap_lazy", &nano::json_handler::bootstrap_lazy);
	no_arg_funcs.emplace ("bootstrap_status", &nano::json_handler::bootstrap_status);
	no_arg_funcs.emplace ("confirmation_active", &nano::json_handler::confirmation_active);
	no_arg_funcs.emplace ("confirmation_history", &nano::json_handler::confirmation_history);
	no_arg_funcs.emplace ("confirmation_info", &nano::json_handler::confirmation_info);
	no_arg_funcs.emplace ("confirmation_quorum", &nano::json_handler::confirmation_quorum);
	no_arg_funcs.emplace ("database_txn_tracker", &nano::json_handler::database_txn_tracker);
	no_arg_funcs.emplace ("delegators", &nano::json_handler::delegators);
	no_arg_funcs.emplace ("delegators_count", &nano::json_handler::delegators_count);
	no_arg_funcs.emplace ("deterministic_key", &nano::json_handler::deterministic_key);
	no_arg_funcs.emplace ("election_statistics", &nano::json_handler::election_statistics);
	no_arg_funcs.emplace ("epoch_upgrade", &nano::json_handler::epoch_upgrade);
	no_arg_funcs.emplace ("frontiers", &nano::json_handler::frontiers);
	no_arg_funcs.emplace ("frontier_count", &nano::json_handler::account_count);
	no_arg_funcs.emplace ("keepalive", &nano::json_handler::keepalive);
	no_arg_funcs.emplace ("key_create", &nano::json_handler::key_create);
	no_arg_funcs.emplace ("key_expand", &nano::json_handler::key_expand);
	no_arg_funcs.emplace ("ledger", &nano::json_handler::ledger);
	no_arg_funcs.emplace ("node_id", &nano::json_handler::node_id);
	no_arg_funcs.emplace ("node_id_delete", &nano::json_handler::node_id_delete);
	no_arg_funcs.emplace ("password_change", &nano::json_handler::password_change);
	no_arg_funcs.emplace ("password_enter", &nano::json_handler::password_enter);
	no_arg_funcs.emplace ("wallet_unlock", &nano::json_handler::password_enter);
	no_arg_funcs.emplace ("peers", &nano::json_handler::peers);
	no_arg_funcs.emplace ("pending", &nano::json_handler::pending);
	no_arg_funcs.emplace ("pending_exists", &nano::json_handler::pending_exists);
	no_arg_funcs.emplace ("receivable", &nano::json_handler::receivable);
	no_arg_funcs.emplace ("receivable_exists", &nano::json_handler::receivable_exists);
	no_arg_funcs.emplace ("process", &nano::json_handler::process);
	no_arg_funcs.emplace ("pruned_exists", &nano::json_handler::pruned_exists);
	no_arg_funcs.emplace ("receive", &nano::json_handler::receive);
	no_arg_funcs.emplace ("receive_minimum", &nano::json_handler::receive_minimum);
	no_arg_funcs.emplace ("receive_minimum_set", &nano::json_handler::receive_minimum_set);
	no_arg_funcs.emplace ("representatives", &nano::json_handler::representatives);
	no_arg_funcs.emplace ("representatives_online", &nano::json_handler::representatives_online);
	no_arg_funcs.emplace ("republish", &nano::json_handler::republish);
	no_arg_funcs.emplace ("search_pending", &nano::json_handler::search_pending);
	no_arg_funcs.emplace ("search_receivable", &nano::json_handler::search_receivable);
	no_arg_funcs.emplace ("search_pending_all", &nano::json_handler::search_pending_all);
	no_arg_funcs.emplace ("search_receivable_all", &nano::json_handler::search_receivable_all);
	no_arg_funcs.emplace ("send", &nano::json_handler::send);
	no_arg_funcs.emplace ("sign", &nano::json_handler::sign);
	no_arg_funcs.emplace ("stats", &nano::json_handler::stats);
	no_arg_funcs.emplace ("stats_clear", &nano::json_handler::stats_clear);
	no_arg_funcs.emplace ("stop", &nano::json_handler::stop);
	no_arg_funcs.emplace ("telemetry", &nano::json_handler::telemetry);
	no_arg_funcs.emplace ("unchecked", &nano::json_handler::unchecked);
	no_arg_funcs.emplace ("unchecked_clear", &nano::json_handler::unchecked_clear);
	no_arg_funcs.emplace ("unchecked_get", &nano::json_handler::unchecked_get);
	no_arg_funcs.emplace ("unchecked_keys", &nano::json_handler::unchecked_keys);
	no_arg_funcs.emplace ("unopened", &nano::json_handler::unopened);
	no_arg_funcs.emplace ("uptime", &nano::json_handler::uptime);
	no_arg_funcs.emplace ("validate_account_number", &nano::json_handler::validate_account_number);
	no_arg_funcs.emplace ("version", &nano::json_handler::version);
	no_arg_funcs.emplace ("wallet_add", &nano::json_handler::wallet_add);
	no_arg_funcs.emplace ("wallet_add_watch", &nano::json_handler::wallet_add_watch);
	no_arg_funcs.emplace ("wallet_balances", &nano::json_handler::wallet_balances);
	no_arg_funcs.emplace ("wallet_change_seed", &nano::json_handler::wallet_change_seed);
	no_arg_funcs.emplace ("wallet_contains", &nano::json_handler::wallet_contains);
	no_arg_funcs.emplace ("wallet_create", &nano::json_handler::wallet_create);
	no_arg_funcs.emplace ("wallet_destroy", &nano::json_handler::wallet_destroy);
	no_arg_funcs.emplace ("wallet_export", &nano::json_handler::wallet_export);
	no_arg_funcs.emplace ("wallet_frontiers", &nano::json_handler::wallet_frontiers);
	no_arg_funcs.emplace ("wallet_history", &nano::json_handler::wallet_history);
	no_arg_funcs.emplace ("wallet_info", &nano::json_handler::wallet_info);
	no_arg_funcs.emplace ("wallet_balance_total", &nano::json_handler::wallet_info);
	no_arg_funcs.emplace ("wallet_key_valid", &nano::json_handler::wallet_key_valid);
	no_arg_funcs.emplace ("wallet_ledger", &nano::json_handler::wallet_ledger);
	no_arg_funcs.emplace ("wallet_lock", &nano::json_handler::wallet_lock);
	no_arg_funcs.emplace ("wallet_pending", &nano::json_handler::wallet_pending);
	no_arg_funcs.emplace ("wallet_receivable", &nano::json_handler::wallet_receivable);
	no_arg_funcs.emplace ("wallet_representative", &nano::json_handler::wallet_representative);
	no_arg_funcs.emplace ("wallet_representative_set", &nano::json_handler::wallet_representative_set);
	no_arg_funcs.emplace ("wallet_republish", &nano::json_handler::wallet_republish);
	no_arg_funcs.emplace ("wallet_work_get", &nano::json_handler::wallet_work_get);
	no_arg_funcs.emplace ("work_generate", &nano::json_handler::work_generate);
	no_arg_funcs.emplace ("work_cancel", &nano::json_handler::work_cancel);
	no_arg_funcs.emplace ("work_get", &nano::json_handler::work_get);
	no_arg_funcs.emplace ("work_set", &nano::json_handler::work_set);
	no_arg_funcs.emplace ("work_validate", &nano::json_handler::work_validate);
	no_arg_funcs.emplace ("work_peer_add", &nano::json_handler::work_peer_add);
	no_arg_funcs.emplace ("work_peers", &nano::json_handler::work_peers);
	no_arg_funcs.emplace ("work_peers_clear", &nano::json_handler::work_peers_clear);
	no_arg_funcs.emplace ("populate_backlog", &nano::json_handler::populate_backlog);
	no_arg_funcs.emplace ("bootstrap_priorities", &nano::json_handler::bootstrap_priorities);
	no_arg_funcs.emplace ("bootstrap_reset", &nano::json_handler::bootstrap_reset);
	return no_arg_funcs;
}

/** Due to the asynchronous nature of updating confirmation heights, it can also be necessary to check active roots */
bool block_confirmed (nano::node & node, nano::secure::transaction & transaction, nano::block_hash const & hash, bool include_active, bool include_only_confirmed)
{
	bool is_confirmed = false;
	if (include_active && !include_only_confirmed)
	{
		is_confirmed = true;
	}
	// Check whether the confirmation height is set
	else if (node.ledger.confirmed.block_exists_or_pruned (transaction, hash))
	{
		is_confirmed = true;
	}
	// This just checks it's not currently undergoing an active transaction
	else if (!include_only_confirmed)
	{
		auto block = node.ledger.any.block_get (transaction, hash);
		is_confirmed = (block != nullptr && !node.active.active (*block));
	}

	return is_confirmed;
}

char const * epoch_as_string (nano::epoch epoch)
{
	switch (epoch)
	{
		case nano::epoch::epoch_2:
			return "2";
		case nano::epoch::epoch_1:
			return "1";
		default:
			return "0";
	}
}
}
