#pragma once

#include <boost/json.hpp>

#include <string>

namespace nano::rpc::v3
{
/**
 * Utility class for building standardized v3 API responses.
 * All v3 responses follow a consistent format with success/data/error envelope.
 */
class response_builder
{
public:
	/**
	 * Build a success response.
	 * @param data The response data (will be placed in "data" field)
	 * @return JSON object with success=true, data, error=null
	 */
	static boost::json::object success (boost::json::value data);

	/**
	 * Build an error response.
	 * @param code Error code string (e.g., "ACCOUNT_NOT_FOUND")
	 * @param message Human-readable error message
	 * @param details Optional additional error details
	 * @return JSON object with success=false, data=null, error object
	 */
	static boost::json::object error (
	std::string const & code,
	std::string const & message,
	boost::json::object details = {});

	/**
	 * Serialize a JSON object to string.
	 * @param obj The JSON object to serialize
	 * @return JSON string
	 */
	static std::string serialize (boost::json::object const & obj);
};
}
