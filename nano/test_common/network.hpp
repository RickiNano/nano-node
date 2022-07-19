#pragma once

#include <nano/boost/asio/read.hpp>
#include <nano/boost/asio/write.hpp>
#include <nano/node/common.hpp>
#include <nano/test_common/system.hpp>

#include <boost/asio/streambuf.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <string>

namespace nano
{
class node;

namespace transport
{
	class channel;
	class channel_tcp;
}

namespace test
{
	class system;
	/** Waits until a TCP connection is established and returns the TCP channel on success*/
	std::shared_ptr<nano::transport::channel_tcp> establish_tcp (nano::test::system &, nano::node &, nano::endpoint const &);

	/** Adds a node to the system without establishing connections */
	std::shared_ptr<nano::node> add_outer_node (nano::test::system & system, uint16_t port_a = nano::test::get_available_port ());
}
}
