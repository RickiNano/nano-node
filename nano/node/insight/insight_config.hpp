#pragma once

#include <nano/lib/constants.hpp>
#include <nano/lib/errors.hpp>

#include <cstdint>
#include <string>

namespace nano
{
class tomlconfig;
namespace insight
{
	/** Insight web dashboard configuration */
	class config final
	{
	public:
		config (nano::network_constants const &);

		nano::error deserialize_toml (nano::tomlconfig &);
		nano::error serialize_toml (nano::tomlconfig &) const;

		nano::network_constants const & network_constants;
		bool enabled{ false };
		uint16_t port{ 17080 };
		std::string address;
		/** How often (ms) the cached state snapshot is refreshed in the background. */
		unsigned refresh_interval{ 1000 };
	};
}
}
