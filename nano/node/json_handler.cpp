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

#include <nlohmann/json.hpp>

namespace
{
void construct_json (nano::container_info_component * component, boost::property_tree::ptree & parent);
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
		std::stringstream istream (body);
		boost::property_tree::read_json (istream, request);
		action = request.get<std::string> ("action");
		auto no_arg_func_iter = ipc_json_handler_no_arg_funcs.find (action);
		if (no_arg_func_iter != ipc_json_handler_no_arg_funcs.cend ())
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
		boost::property_tree::ptree response_error;
		response_error.put ("error", ec.message ());
		std::stringstream ostream;
		boost::property_tree::write_json (ostream, response_error);
		response (ostream.str ());
	}
	else
	{
		response (json_response.dump());
	}
}

void nano::json_handler::uptime ()
{
	json_response["seconds"] = std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - node.startup_time).count ();
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
}
