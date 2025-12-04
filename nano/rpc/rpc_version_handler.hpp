#pragma once

#include <functional>
#include <memory>
#include <string>

namespace nano
{
class node;
class node_rpc_config;

/**
 * Abstract interface for versioned RPC handlers.
 * Each RPC API version (v3, v4, etc.) will implement this interface.
 */
class rpc_version_handler
{
public:
	virtual ~rpc_version_handler () = default;

	/**
	 * Process an RPC request for this version.
	 * @param body The request body (typically JSON string)
	 * @param response Callback to send the response
	 */
	virtual void process_request (
	std::string const & body,
	std::function<void (std::string const &)> response) = 0;

	/**
	 * Get the API version number handled by this handler.
	 * @return Version number (e.g., 3 for v3, 4 for v4)
	 */
	virtual int version () const = 0;
};
}
