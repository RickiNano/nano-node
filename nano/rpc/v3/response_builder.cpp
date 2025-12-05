#include <nano/rpc/v3/response_builder.hpp>

namespace nano::rpc::v3
{
boost::json::object response_builder::success (boost::json::object data)
{
	// Return data directly without envelope for backward compatibility
	return data;
}

boost::json::object response_builder::error (std::string const & message)
{
	// Return simple error format matching legacy API
	boost::json::object response;
	response["error"] = message;
	return response;
}

std::string response_builder::serialize (boost::json::object const & obj)
{
	return boost::json::serialize (obj);
}
}
