#include <nano/boost/asio/ip/address.hpp>
#include <nano/boost/asio/ip/tcp.hpp>
#include <nano/boost/beast/core/flat_buffer.hpp>
#include <nano/boost/beast/http.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/thread_pool.hpp>
#include <nano/node/insight/insight_config.hpp>
#include <nano/node/insight/insight_server.hpp>
#include <nano/node/node.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>

namespace
{
namespace beast = boost::beast;
namespace http = boost::beast::http;
using tcp = boost::asio::ip::tcp;

/** Handles a single HTTP request/response and then closes the connection. */
class session final : public std::enable_shared_from_this<session>
{
public:
	session (tcp::socket socket, std::shared_ptr<nano::insight::server> server) :
		socket{ std::move (socket) },
		server{ std::move (server) }
	{
	}

	void run ()
	{
		auto this_l{ shared_from_this () };
		http::async_read (socket, buffer, request, [this_l] (beast::error_code ec, std::size_t) {
			if (!ec)
			{
				this_l->respond ();
			}
		});
	}

private:
	void respond ()
	{
		response.version (request.version ());
		response.keep_alive (false);
		response.set (http::field::server, "nano-insight");
		response.set (http::field::access_control_allow_origin, "*");

		// Strip any query string from the target
		std::string target = request.target ();
		auto const query = target.find ('?');
		if (query != std::string::npos)
		{
			target.erase (query);
		}

		if (request.method () != http::verb::get)
		{
			response.result (http::status::method_not_allowed);
			response.set (http::field::content_type, "text/plain");
			response.body () = "Only GET is supported";
		}
		else if (target == "/" || target == "/index.html")
		{
			response.result (http::status::ok);
			response.set (http::field::content_type, "text/html; charset=utf-8");
			response.body () = std::string{ nano::insight::index_html () };
		}
		else if (target == "/api/snapshot")
		{
			response.result (http::status::ok);
			response.set (http::field::content_type, "application/json");
			response.body () = server->snapshot_json ();
		}
		else
		{
			response.result (http::status::not_found);
			response.set (http::field::content_type, "text/plain");
			response.body () = "Not found";
		}

		response.prepare_payload ();

		auto this_l{ shared_from_this () };
		http::async_write (socket, response, [this_l] (beast::error_code ec, std::size_t) {
			beast::error_code ignored;
			this_l->socket.shutdown (tcp::socket::shutdown_send, ignored);
		});
	}

	tcp::socket socket;
	std::shared_ptr<nano::insight::server> server;
	beast::flat_buffer buffer;
	http::request<http::string_body> request;
	http::response<http::string_body> response;
};
}

nano::insight::server::server (nano::insight::config const & config_a, nano::node & node_a, boost::asio::io_context & io_ctx_a, nano::logger & logger_a) :
	config{ config_a },
	node{ node_a },
	logger{ logger_a },
	collector{ node_a },
	acceptor{ io_ctx_a }
{
}

void nano::insight::server::start ()
{
	if (!config.enabled)
	{
		return;
	}

	boost::system::error_code ec;
	tcp::endpoint endpoint{ boost::asio::ip::make_address (config.address, ec), config.port };
	if (ec)
	{
		logger.error (nano::log::type::daemon, "Insight: invalid bind address '{}': {}", config.address, ec.message ());
		return;
	}

	acceptor.open (endpoint.protocol (), ec);
	if (!ec)
	{
		acceptor.set_option (boost::asio::socket_base::reuse_address (true), ec);
	}
	if (!ec)
	{
		acceptor.bind (endpoint, ec);
	}
	if (!ec)
	{
		acceptor.listen (boost::asio::socket_base::max_listen_connections, ec);
	}
	if (ec)
	{
		logger.error (nano::log::type::daemon, "Insight: failed to listen on {}:{}: {}", config.address, config.port, ec.message ());
		return;
	}

	logger.info (nano::log::type::daemon, "Insight dashboard listening on http://{}:{}", config.address, config.port);

	// Build an initial snapshot synchronously so the first request is served immediately,
	// then keep it fresh in the background so requests never compute on the networking threads.
	refresh ();

	accept ();
}

void nano::insight::server::stop ()
{
	stopped = true;
	boost::system::error_code ignored;
	acceptor.close (ignored);
}

void nano::insight::server::accept ()
{
	auto this_l{ shared_from_this () };
	acceptor.async_accept ([this_l] (boost::system::error_code const & ec, tcp::socket socket) {
		if (this_l->stopped)
		{
			return;
		}
		if (!ec)
		{
			std::make_shared<session> (std::move (socket), this_l)->run ();
		}
		else
		{
			this_l->logger.debug (nano::log::type::daemon, "Insight: accept error: {}", ec.message ());
		}
		this_l->accept ();
	});
}

void nano::insight::server::refresh ()
{
	if (stopped)
	{
		return;
	}

	auto tree = collector.snapshot ();
	std::ostringstream ss;
	boost::property_tree::write_json (ss, tree);

	{
		nano::lock_guard<nano::mutex> lock{ snapshot_mutex };
		cached_snapshot = std::make_shared<std::string const> (ss.str ());
	}

	if (stopped)
	{
		return;
	}

	// Reschedule on the worker pool so the snapshot is rebuilt off the networking threads.
	// Capture a shared_ptr to keep the server alive while a refresh task is pending.
	auto interval = std::chrono::milliseconds{ std::max (config.refresh_interval, 1u) };
	auto this_l{ shared_from_this () };
	node.workers.post_delayed (interval, [this_l] () {
		this_l->refresh ();
	});
}

std::string nano::insight::server::snapshot_json ()
{
	std::shared_ptr<std::string const> snapshot;
	{
		nano::lock_guard<nano::mutex> lock{ snapshot_mutex };
		snapshot = cached_snapshot;
	}
	// Serving the request is just a copy of the cached string; no node state is read here.
	return snapshot ? *snapshot : std::string{ "{}" };
}
