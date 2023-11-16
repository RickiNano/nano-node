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

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <chrono>
#include <vector>

namespace
{
void construct_json (nano::container_info_component * component, boost::property_tree::ptree & parent);
using ipc_json_handler_no_arg_func_map = std::unordered_map<std::string, std::function<void (nano::json_handler *)>>;
ipc_json_handler_no_arg_func_map create_ipc_json_handler_no_arg_func_map ();
auto ipc_json_handler_no_arg_funcs = create_ipc_json_handler_no_arg_func_map ();
bool block_confirmed (nano::node & node, nano::store::transaction & transaction, nano::block_hash const & hash, bool include_active, bool include_only_confirmed);
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
		std::stringstream istream (body);
		boost::property_tree::read_json (istream, request);
		if (node_rpc_config.request_callback)
		{
			debug_assert (node.network_params.network.is_dev_network ());
			node_rpc_config.request_callback (request);
		}
		action = request.get<std::string> ("action");
		auto no_arg_func_iter = ipc_json_handler_no_arg_funcs.find (action);
		if (no_arg_func_iter != ipc_json_handler_no_arg_funcs.cend ())
		{
			// First try the map of options with no arguments
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
	if (!ec && response_l.empty ())
	{
		// Return an error code if no response data was given
		ec = nano::error_rpc::empty_response;
	}
	if (ec)
	{
		boost::property_tree::ptree response_error;
		response_error.put ("error", ec.message ());
		std::stringstream ostream;
		boost::property_tree::write_json (ostream, response_error);
		response (ostream.str ());
	}
	else
	{
		std::stringstream ostream;
		boost::property_tree::write_json (ostream, response_l);
		response (ostream.str ());
	}
}

std::shared_ptr<nano::wallet> nano::json_handler::wallet_impl ()
{
	if (!ec)
	{
		std::string wallet_text (request.get<std::string> ("wallet"));
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

bool nano::json_handler::wallet_locked_impl (store::transaction const & transaction_a, std::shared_ptr<nano::wallet> const & wallet_a)
{
	bool result (false);
	if (!ec)
	{
		if (!wallet_a->store.valid_password (transaction_a))
		{
			ec = nano::error_common::wallet_locked;
			result = true;
		}
	}
	return result;
}

bool nano::json_handler::wallet_account_impl (store::transaction const & transaction_a, std::shared_ptr<nano::wallet> const & wallet_a, nano::account const & account_a)
{
	bool result (false);
	if (!ec)
	{
		if (wallet_a->store.find (transaction_a, account_a) != wallet_a->store.end ())
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
			account_text = request.get<std::string> ("account");
		}
		if (result.decode_account (account_text))
		{
			ec = ec_a;
		}
		else if (account_text[3] == '-' || account_text[4] == '-')
		{
			// nano- and xrb- prefixes are deprecated
			response_l.put ("deprecated_account_format", "1");
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

nano::amount nano::json_handler::amount_impl ()
{
	nano::amount result (0);
	if (!ec)
	{
		std::string amount_text (request.get<std::string> ("amount"));
		if (result.decode_dec (amount_text))
		{
			ec = nano::error_common::invalid_amount;
		}
	}
	return result;
}

std::shared_ptr<nano::block> nano::json_handler::block_impl (bool signature_work_required)
{
	bool const json_block_l = request.get<bool> ("json_block", false);
	std::shared_ptr<nano::block> result{ nullptr };
	if (!ec)
	{
		boost::property_tree::ptree block_l;
		if (json_block_l)
		{
			block_l = request.get_child ("block");
		}
		else
		{
			std::string block_text (request.get<std::string> ("block"));
			std::stringstream block_stream (block_text);
			try
			{
				boost::property_tree::read_json (block_stream, block_l);
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
				block_l.put ("signature", "0");
				block_l.put ("work", "0");
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
		std::string hash_text (request.get<std::string> (search_text));
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
	boost::optional<std::string> threshold_text (request.get_optional<std::string> ("threshold"));
	if (!ec && threshold_text.is_initialized ())
	{
		if (result.decode_dec (threshold_text.get ()))
		{
			ec = nano::error_common::bad_threshold;
		}
	}
	return result;
}

uint64_t nano::json_handler::work_optional_impl ()
{
	uint64_t result (0);
	boost::optional<std::string> work_text (request.get_optional<std::string> ("work"));
	if (!ec && work_text.is_initialized ())
	{
		if (nano::from_string_hex (work_text.get (), result))
		{
			ec = nano::error_common::bad_work_format;
		}
	}
	return result;
}

uint64_t nano::json_handler::difficulty_optional_impl (nano::work_version const version_a)
{
	auto difficulty (node.default_difficulty (version_a));
	boost::optional<std::string> difficulty_text (request.get_optional<std::string> ("difficulty"));
	if (!ec && difficulty_text.is_initialized ())
	{
		if (nano::from_string_hex (difficulty_text.get (), difficulty))
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
	auto transaction (node.store.tx_begin_read ());
	// Previous block find
	std::shared_ptr<nano::block> block_previous (nullptr);
	auto previous (block_a.previous ());
	if (!previous.is_zero ())
	{
		block_previous = node.store.block.get (transaction, previous);
	}
	// Send check
	if (block_previous != nullptr)
	{
		details.is_send = node.ledger.balance (transaction, previous) > block_a.balance ().number ();
		details_found = true;
	}
	// Epoch check
	if (block_previous != nullptr)
	{
		details.epoch = block_previous->sideband ().details.epoch;
	}
	auto link (block_a.link ());
	if (!link.is_zero () && !details.is_send)
	{
		auto block_link (node.store.block.get (transaction, link.as_block_hash ()));
		if (block_link != nullptr && node.store.pending.exists (transaction, nano::pending_key (block_a.account (), link.as_block_hash ())))
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
	boost::optional<std::string> multiplier_text (request.get_optional<std::string> ("multiplier"));
	if (!ec && multiplier_text.is_initialized ())
	{
		auto success = boost::conversion::try_lexical_convert<double> (multiplier_text.get (), multiplier);
		if (success && multiplier > 0.)
		{
			difficulty = nano::difficulty::from_multiplier (multiplier, node.default_difficulty (version_a));
		}
		else
		{
			ec = nano::error_rpc::bad_multiplier_format;
		}
	}
	return multiplier;
}

nano::work_version nano::json_handler::work_version_optional_impl (nano::work_version const default_a)
{
	nano::work_version result = default_a;
	boost::optional<std::string> version_text (request.get_optional<std::string> ("version"));
	if (!ec && version_text.is_initialized ())
	{
		if (*version_text == nano::to_string (nano::work_version::work_1))
		{
			result = nano::work_version::work_1;
		}
		else
		{
			ec = nano::error_rpc::bad_work_version;
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
		std::string count_text (request.get<std::string> ("count"));
		if (decode_unsigned (count_text, result) || result == 0)
		{
			ec = nano::error_common::invalid_count;
		}
	}
	return result;
}

uint64_t nano::json_handler::count_optional_impl (uint64_t result)
{
	boost::optional<std::string> count_text (request.get_optional<std::string> ("count"));
	if (!ec && count_text.is_initialized ())
	{
		if (decode_unsigned (count_text.get (), result))
		{
			ec = nano::error_common::invalid_count;
		}
	}
	return result;
}

uint64_t nano::json_handler::offset_optional_impl (uint64_t result)
{
	boost::optional<std::string> offset_text (request.get_optional<std::string> ("offset"));
	if (!ec && offset_text.is_initialized ())
	{
		if (decode_unsigned (offset_text.get (), result))
		{
			ec = nano::error_rpc::invalid_offset;
		}
	}
	return result;
}

namespace
{
class history_visitor : public nano::block_visitor
{
public:
	history_visitor (nano::json_handler & handler_a, bool raw_a, nano::store::transaction & transaction_a, boost::property_tree::ptree & tree_a, nano::block_hash const & hash_a, std::vector<nano::public_key> const & accounts_filter_a) :
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
		tree.put ("type", "send");
		auto account (block_a.hashables.destination.to_account ());
		tree.put ("account", account);
		bool error_or_pruned (false);
		auto amount (handler.node.ledger.amount_safe (transaction, hash, error_or_pruned).convert_to<std::string> ());
		if (!error_or_pruned)
		{
			tree.put ("amount", amount);
		}
		if (raw)
		{
			tree.put ("destination", account);
			tree.put ("balance", block_a.hashables.balance.to_string_dec ());
			tree.put ("previous", block_a.hashables.previous.to_string ());
		}
	}
	void receive_block (nano::receive_block const & block_a)
	{
		tree.put ("type", "receive");
		bool error_or_pruned (false);
		auto amount (handler.node.ledger.amount_safe (transaction, hash, error_or_pruned).convert_to<std::string> ());
		if (!error_or_pruned)
		{
			auto source_account (handler.node.ledger.account_safe (transaction, block_a.hashables.source, error_or_pruned));
			if (!error_or_pruned)
			{
				tree.put ("account", source_account.to_account ());
			}
			tree.put ("amount", amount);
		}
		if (raw)
		{
			tree.put ("source", block_a.hashables.source.to_string ());
			tree.put ("previous", block_a.hashables.previous.to_string ());
		}
	}
	void open_block (nano::open_block const & block_a)
	{
		if (raw)
		{
			tree.put ("type", "open");
			tree.put ("representative", block_a.hashables.representative.to_account ());
			tree.put ("source", block_a.hashables.source.to_string ());
			tree.put ("opened", block_a.hashables.account.to_account ());
		}
		else
		{
			// Report opens as a receive
			tree.put ("type", "receive");
		}
		if (block_a.hashables.source != handler.node.ledger.constants.genesis->account ())
		{
			bool error_or_pruned (false);
			auto amount (handler.node.ledger.amount_safe (transaction, hash, error_or_pruned).convert_to<std::string> ());
			if (!error_or_pruned)
			{
				auto source_account (handler.node.ledger.account_safe (transaction, block_a.hashables.source, error_or_pruned));
				if (!error_or_pruned)
				{
					tree.put ("account", source_account.to_account ());
				}
				tree.put ("amount", amount);
			}
		}
		else
		{
			tree.put ("account", handler.node.ledger.constants.genesis->account ().to_account ());
			tree.put ("amount", nano::dev::constants.genesis_amount.convert_to<std::string> ());
		}
	}
	void change_block (nano::change_block const & block_a)
	{
		if (raw && accounts_filter.empty ())
		{
			tree.put ("type", "change");
			tree.put ("representative", block_a.hashables.representative.to_account ());
			tree.put ("previous", block_a.hashables.previous.to_string ());
		}
	}
	void state_block (nano::state_block const & block_a)
	{
		if (raw)
		{
			tree.put ("type", "state");
			tree.put ("representative", block_a.hashables.representative.to_account ());
			tree.put ("link", block_a.hashables.link.to_string ());
			tree.put ("balance", block_a.hashables.balance.to_string_dec ());
			tree.put ("previous", block_a.hashables.previous.to_string ());
		}
		auto balance (block_a.hashables.balance.number ());
		bool error_or_pruned (false);
		auto previous_balance (handler.node.ledger.balance_safe (transaction, block_a.hashables.previous, error_or_pruned));
		if (error_or_pruned)
		{
			if (raw)
			{
				tree.put ("subtype", "unknown");
			}
			else
			{
				tree.put ("type", "unknown");
			}
		}
		else if (balance < previous_balance)
		{
			if (should_ignore_account (block_a.hashables.link.as_account ()))
			{
				tree.clear ();
				return;
			}
			if (raw)
			{
				tree.put ("subtype", "send");
			}
			else
			{
				tree.put ("type", "send");
			}
			tree.put ("account", block_a.hashables.link.to_account ());
			tree.put ("amount", (previous_balance - balance).convert_to<std::string> ());
		}
		else
		{
			if (block_a.hashables.link.is_zero ())
			{
				if (raw && accounts_filter.empty ())
				{
					tree.put ("subtype", "change");
				}
			}
			else if (balance == previous_balance && handler.node.ledger.is_epoch_link (block_a.hashables.link))
			{
				if (raw && accounts_filter.empty ())
				{
					tree.put ("subtype", "epoch");
					tree.put ("account", handler.node.ledger.epoch_signer (block_a.link ()).to_account ());
				}
			}
			else
			{
				auto source_account (handler.node.ledger.account_safe (transaction, block_a.hashables.link.as_block_hash (), error_or_pruned));
				if (!error_or_pruned && should_ignore_account (source_account))
				{
					tree.clear ();
					return;
				}
				if (raw)
				{
					tree.put ("subtype", "receive");
				}
				else
				{
					tree.put ("type", "receive");
				}
				if (!error_or_pruned)
				{
					tree.put ("account", source_account.to_account ());
				}
				tree.put ("amount", (balance - previous_balance).convert_to<std::string> ());
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
	nano::store::transaction & transaction;
	boost::property_tree::ptree & tree;
	nano::block_hash const & hash;
	std::vector<nano::public_key> const & accounts_filter;
};
}

void nano::json_handler::uptime ()
{
	response_l.put ("seconds", std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - node.startup_time).count ());
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
void construct_json (nano::container_info_component * component, boost::property_tree::ptree & parent)
{
	// We are a leaf node, print name and exit
	if (!component->is_composite ())
	{
		auto & leaf_info = static_cast<nano::container_info_leaf *> (component)->get_info ();
		boost::property_tree::ptree child;
		child.put ("count", leaf_info.count);
		child.put ("size", leaf_info.count * leaf_info.sizeof_element);
		parent.add_child (leaf_info.name, child);
		return;
	}

	auto composite = static_cast<nano::container_info_composite *> (component);

	boost::property_tree::ptree current;
	for (auto & child : composite->get_children ())
	{
		construct_json (child.get (), current);
	}

	parent.add_child (composite->get_name (), current);
}

// Any RPC handlers which require no arguments (excl default arguments) should go here.
// This is to prevent large if/else chains which compilers can have limits for (MSVC for instance has 128).
ipc_json_handler_no_arg_func_map create_ipc_json_handler_no_arg_func_map ()
{
	ipc_json_handler_no_arg_func_map no_arg_funcs;
	no_arg_funcs.emplace ("uptime", &nano::json_handler::uptime);
	return no_arg_funcs;
}

/** Due to the asynchronous nature of updating confirmation heights, it can also be necessary to check active roots */
bool block_confirmed (nano::node & node, nano::store::transaction & transaction, nano::block_hash const & hash, bool include_active, bool include_only_confirmed)
{
	bool is_confirmed = false;
	if (include_active && !include_only_confirmed)
	{
		is_confirmed = true;
	}
	// Check whether the confirmation height is set
	else if (node.ledger.block_confirmed (transaction, hash))
	{
		is_confirmed = true;
	}
	// This just checks it's not currently undergoing an active transaction
	else if (!include_only_confirmed)
	{
		auto block (node.store.block.get (transaction, hash));
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
