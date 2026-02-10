#pragma once

#include <boost/json.hpp>

#include <functional>
#include <string>

namespace nano
{
inline void json_error_response (std::function<void (std::string const &)> response_a, std::string const & message_a)
{
	boost::json::object response;
	response["error"] = message_a;
	response_a (boost::json::serialize (response));
}
}
