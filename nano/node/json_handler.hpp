#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/ipc/flatbuffers_handler.hpp>
#include <nano/node/wallet.hpp>
#include <nano/rpc/rpc.hpp>

#include <boost/property_tree/ptree.hpp>

#include <functional>
#include <string>

namespace nano
{
namespace ipc
{
	class ipc_server;
}
class node;
class node_rpc_config;

class json_handler : public std::enable_shared_from_this<nano::json_handler>
{
public:
	json_handler (
	nano::node &, nano::node_rpc_config const &, std::string const &, std::function<void (std::string const &)> const &, std::function<void ()> stop_callback = [] () {});
	void process_request (bool unsafe = false);
	void uptime ();
	std::string body;
	nano::node & node;
	boost::property_tree::ptree request;
	std::function<void (std::string const &)> response;
	void response_errors ();
	std::error_code ec;
	std::string action;
	boost::property_tree::ptree response_l;
	std::shared_ptr<nano::wallet> wallet_impl ();
	bool wallet_locked_impl (store::transaction const &, std::shared_ptr<nano::wallet> const &);
	bool wallet_account_impl (store::transaction const &, std::shared_ptr<nano::wallet> const &, nano::account const &);
	nano::account account_impl (std::string = "", std::error_code = nano::error_common::bad_account_number);
	nano::account_info account_info_impl (store::transaction const &, nano::account const &);
	nano::amount amount_impl ();
	std::shared_ptr<nano::block> block_impl (bool = true);
	nano::block_hash hash_impl (std::string = "hash");
	nano::amount threshold_optional_impl ();
	uint64_t work_optional_impl ();
	uint64_t count_impl ();
	uint64_t count_optional_impl (uint64_t = std::numeric_limits<uint64_t>::max ());
	uint64_t offset_optional_impl (uint64_t = 0);
	uint64_t difficulty_optional_impl (nano::work_version const);
	uint64_t difficulty_ledger (nano::block const &);
	double multiplier_optional_impl (nano::work_version const, uint64_t &);
	nano::work_version work_version_optional_impl (nano::work_version const default_a);
	bool enable_sign_hash{ false };
	std::function<void ()> stop_callback;
	nano::node_rpc_config const & node_rpc_config;
	std::function<void ()> create_worker_task (std::function<void (std::shared_ptr<nano::json_handler> const &)> const &);
};

class inprocess_rpc_handler final : public nano::rpc_handler_interface
{
public:
	inprocess_rpc_handler (
	nano::node & node_a, nano::ipc::ipc_server & ipc_server_a, nano::node_rpc_config const & node_rpc_config_a, std::function<void ()> stop_callback_a = [] () {}) :
		node (node_a),
		ipc_server (ipc_server_a),
		stop_callback (stop_callback_a),
		node_rpc_config (node_rpc_config_a)
	{
	}

	void process_request (std::string const &, std::string const & body_a, std::function<void (std::string const &)> response_a) override;
	void process_request_v2 (rpc_handler_request_params const & params_a, std::string const & body_a, std::function<void (std::shared_ptr<std::string> const &)> response_a) override;

	void stop () override
	{
		if (rpc)
		{
			rpc->stop ();
		}
	}

	void rpc_instance (nano::rpc & rpc_a) override
	{
		rpc = rpc_a;
	}

private:
	nano::node & node;
	nano::ipc::ipc_server & ipc_server;
	boost::optional<nano::rpc &> rpc;
	std::function<void ()> stop_callback;
	nano::node_rpc_config const & node_rpc_config;
};
}
