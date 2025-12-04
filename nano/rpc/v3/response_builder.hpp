#pragma once

#include <boost/json.hpp>

#include <string>

namespace nano::rpc::v3
{
/**
 * Utility class for building API responses compatible with the legacy format.
 * Responses match the old property_tree API format for backward compatibility.
 */
class response_builder
{
public:
	/**
	 * Build a success response.
	 * @param data The response data (returned directly without envelope)
	 * @return JSON object containing the data fields directly
	 */
	static boost::json::object success (boost::json::object data);

	/**
	 * Build an error response.
	 * @param message Human-readable error message
	 * @return JSON object with single "error" field
	 */
	static boost::json::object error (std::string const & message);

	/**
	 * Serialize a JSON object to string.
	 * @param obj The JSON object to serialize
	 * @return JSON string
	 */
	static std::string serialize (boost::json::object const & obj);
};
}
