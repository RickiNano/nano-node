#include <nano/lib/stream.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/telemetry.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/endian/conversion.hpp>

#include <numeric>

using namespace std::chrono_literals;

TEST (telemetry, signatures)
{
	nano::keypair node_id;
	nano::messages::telemetry_data data;
	data.node_id = node_id.pub;
	data.major_version = 20;
	data.minor_version = 1;
	data.patch_version = 5;
	data.pre_release_version = 2;
	data.maker = 1;
	data.timestamp = std::chrono::system_clock::time_point (100ms);
	data.sign (node_id);
	ASSERT_FALSE (data.validate_signature ());
	auto signature = data.signature;
	// Check that the signature is different if changing a piece of data
	data.maker = 2;
	data.sign (node_id);
	ASSERT_NE (data.signature, signature);
}

TEST (telemetry, unknown_data)
{
	nano::keypair node_id;
	nano::messages::telemetry_data data;
	data.node_id = node_id.pub;
	data.major_version = 20;
	data.minor_version = 1;
	data.patch_version = 5;
	data.pre_release_version = 2;
	data.maker = 1;
	data.timestamp = std::chrono::system_clock::time_point (100ms);
	data.unknown_data.push_back (1);
	data.sign (node_id);
	ASSERT_FALSE (data.validate_signature ());
}

TEST (telemetry, no_peers)
{
	nano::test::system system (1);

	auto responses = system.nodes[0]->telemetry.get_all_telemetries ();
	ASSERT_TRUE (responses.empty ());
}

TEST (telemetry, basic)
{
	nano::test::system system;
	nano::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	auto node_server = system.add_node (node_flags);

	// Request telemetry metrics
	auto channel = node_client->network.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);

	std::optional<nano::messages::telemetry_data> telemetry_data;
	ASSERT_TIMELY (5s, telemetry_data = node_client->telemetry.get_telemetry (channel->get_remote_endpoint ()));
	ASSERT_EQ (node_server->get_node_id (), telemetry_data->node_id);

	// Check the metrics are correct
	ASSERT_TRUE (nano::test::compare_telemetry (*telemetry_data, *node_server));

	// Call again straight away
	auto telemetry_data_2 = node_client->telemetry.get_telemetry (channel->get_remote_endpoint ());
	ASSERT_TRUE (telemetry_data_2);

	// Call again straight away
	auto telemetry_data_3 = node_client->telemetry.get_telemetry (channel->get_remote_endpoint ());
	ASSERT_TRUE (telemetry_data_3);

	// we expect at least one consecutive repeat of telemetry
	ASSERT_TRUE (*telemetry_data == telemetry_data_2 || telemetry_data_2 == telemetry_data_3);

	// Wait the cache period and check cache is not used
	WAIT (3s);

	std::optional<nano::messages::telemetry_data> telemetry_data_4;
	ASSERT_TIMELY (5s, telemetry_data_4 = node_client->telemetry.get_telemetry (channel->get_remote_endpoint ()));
	ASSERT_NE (*telemetry_data, *telemetry_data_4);
}

TEST (telemetry, invalid_endpoint)
{
	nano::test::system system (2);

	auto node_client = system.nodes.front ();
	auto node_server = system.nodes.back ();

	node_client->telemetry.trigger ();

	// Give some time for nodes to exchange telemetry
	WAIT (1s);

	nano::endpoint endpoint = *nano::parse_endpoint ("::ffff:240.0.0.0:12345");
	ASSERT_FALSE (node_client->telemetry.get_telemetry (endpoint));
}

TEST (telemetry, disconnected)
{
	nano::test::system system;
	nano::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	auto node_server = system.add_node (node_flags);

	auto channel = node_client->network.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);

	// Ensure telemetry is available before disconnecting
	ASSERT_TIMELY (5s, node_client->telemetry.get_telemetry (channel->get_remote_endpoint ()));

	system.stop_node (*node_server);
	ASSERT_TRUE (channel);

	// Ensure telemetry from disconnected peer is removed
	ASSERT_TIMELY (5s, !node_client->telemetry.get_telemetry (channel->get_remote_endpoint ()));
}

TEST (telemetry, dos_tcp)
{
	// Confirm that telemetry_reqs are not processed
	nano::test::system system;
	nano::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	auto node_server = system.add_node (node_flags);

	auto channel = node_client->network.tcp_channels.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);

	nano::messages::telemetry_req message{ nano::dev::network_params.network };
	for (int i = 0; i < 10; ++i)
	{
		channel->send (message, nano::transport::traffic_type::test, [] (boost::system::error_code const & ec, size_t size_a) {
			ASSERT_FALSE (ec);
		});
	}

	// Should process telemetry_req messages
	ASSERT_TIMELY (5s, 1 < node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in));

	// But not respond to all of them (by default there are 2 broadcasts per second in dev mode)
	ASSERT_ALWAYS (1s, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_ack, nano::stat::dir::out) < 7);
}

TEST (telemetry, disable_metrics)
{
	nano::test::system system;
	nano::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	node_flags.disable_providing_telemetry_metrics = true;
	auto node_server = system.add_node (node_flags);

	// Try and request metrics from a node which is turned off but a channel is not closed yet
	auto channel = node_client->network.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);

	node_client->telemetry.trigger ();

	ASSERT_NEVER (1s, node_client->telemetry.get_telemetry (channel->get_remote_endpoint ()));

	// It should still be able to receive metrics though
	auto channel1 = node_server->network.find_node_id (node_client->get_node_id ());
	ASSERT_NE (nullptr, channel1);

	std::optional<nano::messages::telemetry_data> telemetry_data;
	ASSERT_TIMELY (5s, telemetry_data = node_server->telemetry.get_telemetry (channel1->get_remote_endpoint ()));

	ASSERT_TRUE (nano::test::compare_telemetry (*telemetry_data, *node_client));
}

TEST (telemetry, max_possible_size)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_providing_telemetry_metrics = true;
	auto node_client = system.add_node (node_flags);
	auto node_server = system.add_node (node_flags);

	nano::messages::telemetry_data data;
	data.unknown_data.resize (nano::messages::message_header::telemetry_size_mask.to_ulong () - nano::messages::telemetry_data::latest_size);

	nano::messages::telemetry_ack message{ nano::dev::network_params.network, data };

	auto channel = node_client->network.tcp_channels.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);
	channel->send (message, nano::transport::traffic_type::test, [] (boost::system::error_code const & ec, size_t size_a) {
		ASSERT_FALSE (ec);
	});

	ASSERT_TIMELY_EQ (5s, 1, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_ack, nano::stat::dir::in));
}

TEST (telemetry, maker_pruning)
{
	nano::test::system system;
	nano::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	node_flags.enable_pruning = true;
	nano::node_config config;
	config.enable_voting = false;
	auto node_server = system.add_node (config, node_flags);

	// Request telemetry metrics
	auto channel = node_client->network.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);

	std::optional<nano::messages::telemetry_data> telemetry_data;
	ASSERT_TIMELY (5s, telemetry_data = node_client->telemetry.get_telemetry (channel->get_remote_endpoint ()));
	ASSERT_EQ (node_server->get_node_id (), telemetry_data->node_id);

	// Ensure telemetry response indicates pruned node
	ASSERT_EQ (nano::messages::telemetry_maker::nf_pruned_node, static_cast<nano::messages::telemetry_maker> (telemetry_data->maker));
}

TEST (telemetry, invalid_signature)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto telemetry = node.local_telemetry ();
	telemetry.block_count = 9999; // Change data so signature is no longer valid

	auto message = nano::messages::telemetry_ack{ nano::dev::network_params.network, telemetry };
	node.inbound (message, nano::test::fake_channel (node));

	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::telemetry, nano::stat::detail::invalid_signature) > 0);
	ASSERT_ALWAYS (1s, node.stats.count (nano::stat::type::telemetry, nano::stat::detail::process) == 0)
}

TEST (telemetry, mismatched_node_id)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto telemetry = node.local_telemetry ();

	auto message = nano::messages::telemetry_ack{ nano::dev::network_params.network, telemetry };
	node.inbound (message, nano::test::fake_channel (node, /* node id */ { 123 }));

	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::telemetry, nano::stat::detail::node_id_mismatch) > 0);
	ASSERT_ALWAYS (1s, node.stats.count (nano::stat::type::telemetry, nano::stat::detail::process) == 0)
}

TEST (telemetry, ongoing_broadcasts)
{
	nano::test::system system;
	nano::node_flags node_flags;
	auto & node1 = *system.add_node (node_flags);
	auto & node2 = *system.add_node (node_flags);

	ASSERT_TIMELY (5s, node1.stats.count (nano::stat::type::telemetry, nano::stat::detail::process) >= 3);
	ASSERT_TIMELY (5s, node2.stats.count (nano::stat::type::telemetry, nano::stat::detail::process) >= 3)
}

TEST (telemetry, database_backend_field)
{
	nano::keypair node_id;

	// Test all three database_backend values
	for (uint8_t backend_value : { 0, 1, 2 })
	{
		nano::messages::telemetry_data data;
		data.node_id = node_id.pub;
		data.database_backend = backend_value;
		data.sign (node_id);

		// Serialize
		std::vector<uint8_t> bytes;
		{
			nano::vectorstream stream (bytes);
			data.serialize (stream);
		}

		// Deserialize
		nano::messages::telemetry_data data2;
		nano::bufferstream stream (bytes.data (), bytes.size ());
		data2.deserialize (stream, static_cast<uint16_t> (bytes.size ()));
		ASSERT_EQ (data.database_backend, data2.database_backend);
		ASSERT_EQ (backend_value, data2.database_backend);

		// Verify signature is still valid
		ASSERT_FALSE (data2.validate_signature ());
	}
}

TEST (telemetry, database_backend_backwards_compatibility)
{
	// Test: New node (with database_backend) receiving telemetry from old node (without database_backend)
	// The database_backend field should default to 0 since old payload doesn't include it

	nano::keypair node_id;
	nano::messages::telemetry_data old_data;
	old_data.node_id = node_id.pub;
	old_data.block_count = 1000;

	// Create signature for old format (without database_backend)
	std::vector<uint8_t> payload_bytes;
	{
		nano::vectorstream stream (payload_bytes);
		// Serialize payload in OLD format (without database_backend)
		nano::write (stream, old_data.node_id);
		nano::write (stream, boost::endian::native_to_big (old_data.block_count));
		nano::write (stream, boost::endian::native_to_big (old_data.cemented_count));
		nano::write (stream, boost::endian::native_to_big (old_data.unchecked_count));
		nano::write (stream, boost::endian::native_to_big (old_data.account_count));
		nano::write (stream, boost::endian::native_to_big (old_data.bandwidth_cap));
		nano::write (stream, boost::endian::native_to_big (old_data.peer_count));
		nano::write (stream, old_data.protocol_version);
		nano::write (stream, boost::endian::native_to_big (old_data.uptime));
		nano::write (stream, old_data.genesis_block.bytes);
		nano::write (stream, old_data.major_version);
		nano::write (stream, old_data.minor_version);
		nano::write (stream, old_data.patch_version);
		nano::write (stream, old_data.pre_release_version);
		nano::write (stream, old_data.maker);
		nano::write (stream, boost::endian::native_to_big (std::chrono::duration_cast<std::chrono::milliseconds> (old_data.timestamp.time_since_epoch ()).count ()));
		nano::write (stream, boost::endian::native_to_big (old_data.active_difficulty));
		// NO database_backend - this is old format
	}
	old_data.signature = nano::sign_message (node_id.prv, node_id.pub, payload_bytes.data (), payload_bytes.size ());

	// Manually serialize OLD format (without database_backend) - simulating what old nodes send
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		nano::write (stream, old_data.signature);
		nano::write (stream, payload_bytes); // Write the payload we signed
	}

	// New node deserializes old format
	// Calculate actual size: should be bytes.size()
	uint16_t old_total_size = static_cast<uint16_t> (bytes.size ());
	nano::messages::telemetry_data new_node_data;
	nano::bufferstream stream (bytes.data (), bytes.size ());

	// New node's deserialization should handle old format gracefully
	new_node_data.deserialize (stream, old_total_size);

	// database_backend should remain at default value 0 (unknown)
	ASSERT_EQ (0, new_node_data.database_backend);

	// Other fields should match
	ASSERT_EQ (old_data.node_id, new_node_data.node_id);
	ASSERT_EQ (old_data.block_count, new_node_data.block_count);

	// Signature should validate using backwards compatibility path
	ASSERT_FALSE (new_node_data.validate_signature ());
}

TEST (telemetry, database_backend_forwards_compatibility)
{
	// Test: Current node receiving telemetry from future node (with extra fields beyond database_backend)
	// Extra bytes should go into unknown_data for future compatibility

	nano::keypair node_id;
	nano::messages::telemetry_data current_data;
	current_data.node_id = node_id.pub;
	current_data.database_backend = 1; // LMDB

	// Create payload with future fields (simulate what a future node sends)
	std::vector<uint8_t> payload_bytes;
	{
		nano::vectorstream stream (payload_bytes);
		// Serialize current format WITHOUT signature
		nano::write (stream, current_data.node_id);
		nano::write (stream, boost::endian::native_to_big (current_data.block_count));
		nano::write (stream, boost::endian::native_to_big (current_data.cemented_count));
		nano::write (stream, boost::endian::native_to_big (current_data.unchecked_count));
		nano::write (stream, boost::endian::native_to_big (current_data.account_count));
		nano::write (stream, boost::endian::native_to_big (current_data.bandwidth_cap));
		nano::write (stream, boost::endian::native_to_big (current_data.peer_count));
		nano::write (stream, current_data.protocol_version);
		nano::write (stream, boost::endian::native_to_big (current_data.uptime));
		nano::write (stream, current_data.genesis_block.bytes);
		nano::write (stream, current_data.major_version);
		nano::write (stream, current_data.minor_version);
		nano::write (stream, current_data.patch_version);
		nano::write (stream, current_data.pre_release_version);
		nano::write (stream, current_data.maker);
		nano::write (stream, boost::endian::native_to_big (std::chrono::duration_cast<std::chrono::milliseconds> (current_data.timestamp.time_since_epoch ()).count ()));
		nano::write (stream, boost::endian::native_to_big (current_data.active_difficulty));
		nano::write (stream, current_data.database_backend);
		// Simulate future fields: add 4 extra bytes (e.g., future uint32_t field)
		uint32_t future_field = 0x12345678;
		nano::write (stream, boost::endian::native_to_big (future_field));
	}

	// Sign the payload (including future fields)
	auto signature = nano::sign_message (node_id.prv, node_id.pub, payload_bytes.data (), payload_bytes.size ());

	// Construct complete message (signature + payload)
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		nano::write (stream, signature);
		nano::write (stream, payload_bytes);
	}

	// Current node deserializes future format
	// Calculate actual size: should be bytes.size()
	uint16_t future_total_size = static_cast<uint16_t> (bytes.size ());
	nano::messages::telemetry_data current_node_data;
	nano::bufferstream stream (bytes.data (), bytes.size ());

	// Deserialize with future total size
	current_node_data.deserialize (stream, future_total_size);

	// database_backend should be correctly read
	ASSERT_EQ (1, current_node_data.database_backend);

	// Extra 4 bytes should be captured in unknown_data
	ASSERT_EQ (4, current_node_data.unknown_data.size ());

	// Verify the unknown_data contains the future field value
	uint32_t captured_value = 0;
	captured_value |= static_cast<uint32_t> (current_node_data.unknown_data[0]) << 24;
	captured_value |= static_cast<uint32_t> (current_node_data.unknown_data[1]) << 16;
	captured_value |= static_cast<uint32_t> (current_node_data.unknown_data[2]) << 8;
	captured_value |= static_cast<uint32_t> (current_node_data.unknown_data[3]);
	ASSERT_EQ (0x12345678, captured_value);

	// Signature should still validate (unknown_data is included in signature)
	ASSERT_FALSE (current_node_data.validate_signature ());
}
