#include <nano/rpc/v3/response_builder.hpp>

namespace nano::rpc::v3
{
boost::json::object response_builder::success (boost::json::value data)
{
	boost::json::object response;
	response["success"] = true;
	response["data"] = data;
	response["error"] = nullptr;
	return response;
}

boost::json::object response_builder::error (
std::string const & code,
std::string const & message,
boost::json::object details)
{
	boost::json::object error_obj;
	error_obj["code"] = code;
	error_obj["message"] = message;
	if (!details.empty ())
	{
		error_obj["details"] = details;
	}

	boost::json::object response;
	response["success"] = false;
	response["data"] = nullptr;
	response["error"] = error_obj;
	return response;
}

std::string response_builder::serialize (boost::json::object const & obj)
{
	return boost::json::serialize (obj);
}
}
