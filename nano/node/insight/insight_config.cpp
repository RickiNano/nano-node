#include <nano/boost/asio/ip/address_v6.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/insight/insight_config.hpp>

nano::insight::config::config (nano::network_constants const & network_constants) :
	network_constants{ network_constants },
	address{ boost::asio::ip::address_v6::loopback ().to_string () }
{
}

nano::error nano::insight::config::serialize_toml (nano::tomlconfig & toml) const
{
	toml.put ("enable", enabled, "Enable or disable the Insight web dashboard server.\ntype:bool");
	toml.put ("address", address, "Insight dashboard bind address.\ntype:string,ip");
	toml.put ("port", port, "Insight dashboard listening port.\ntype:uint16");
	toml.put ("refresh_interval", refresh_interval, "How often (in milliseconds) the dashboard state snapshot is refreshed in the background.\ntype:uint32");
	return toml.get_error ();
}

nano::error nano::insight::config::deserialize_toml (nano::tomlconfig & toml)
{
	toml.get<bool> ("enable", enabled);
	boost::asio::ip::address_v6 address_l;
	toml.get_optional<boost::asio::ip::address_v6> ("address", address_l, boost::asio::ip::address_v6::loopback ());
	address = address_l.to_string ();
	toml.get<uint16_t> ("port", port);
	toml.get<unsigned> ("refresh_interval", refresh_interval);
	return toml.get_error ();
}
