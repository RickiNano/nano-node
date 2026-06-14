#pragma once

#include <nano/boost/asio/ip/tcp.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/insight/insight_collector.hpp>

#include <nano/lib/locks.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

namespace nano
{
class logger;
}

namespace nano::insight
{
/** Returns the embedded single-page dashboard served at "/". */
std::string_view index_html ();

/**
 * Lightweight HTTP server for the Insight dashboard.
 *
 * Serves the embedded single-page app at "/" and a live JSON state snapshot at
 * "/api/snapshot". The browser polls the snapshot endpoint to render the dashboard,
 * so no live data flows out of the node beyond what is requested.
 *
 * Modeled on `nano::websocket::listener`: an asio acceptor spawns a short-lived
 * session per connection that reads one request, writes one response, and closes.
 */
class server final : public std::enable_shared_from_this<server>
{
public:
	server (nano::insight::config const &, nano::node &, boost::asio::io_context &, nano::logger &);

	void start ();
	void stop ();

	/** Most recent serialized JSON snapshot of node state. Used by the "/api/snapshot" route. */
	std::string snapshot_json ();

private:
	void accept ();

	/** Rebuilds the cached snapshot off the networking threads, then reschedules itself. */
	void refresh ();

	nano::insight::config const & config;
	nano::node & node;
	nano::logger & logger;
	nano::insight::collector collector;
	boost::asio::ip::tcp::acceptor acceptor;
	std::atomic<bool> stopped{ false };

	/** Cached snapshot served to clients, refreshed in the background on a fixed interval. */
	nano::mutex snapshot_mutex;
	std::shared_ptr<std::string const> cached_snapshot;
};
}
