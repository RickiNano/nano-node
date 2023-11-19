#pragma once

#include <nlohmann/json.hpp>

namespace nano
{
inline void json_error_response (std::function<void (std::string const &)> response_a, std::string const & message_a)
{
	nlohmann::json response_l;
	response_l ["error"] = message_a;
	response_a (response_l.dump(4));
}
}
