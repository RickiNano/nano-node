#include <nano/lib/numbers.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/messages/telemetry.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/cementing_set.hpp>
#include <nano/node/election_behavior.hpp>
#include <nano/node/insight/insight_collector.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/online_reps.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/secure/ledger.hpp>

#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>

namespace
{
std::string weight_to_string (nano::uint128_t const & weight)
{
	return nano::amount{ weight }.to_string_dec ();
}
}

nano::insight::collector::collector (nano::node & node_a) :
	node{ node_a }
{
}

boost::property_tree::ptree nano::insight::collector::snapshot ()
{
	boost::property_tree::ptree tree;

	auto const block_count = node.ledger.block_count ();
	auto const cemented_count = node.ledger.cemented_count ();
	auto const messages_in = node.stats.count (nano::stat::type::message, nano::stat::dir::in);
	auto const messages_out = node.stats.count (nano::stat::type::message, nano::stat::dir::out);

	// Ledger totals
	boost::property_tree::ptree ledger_tree;
	ledger_tree.put ("block_count", block_count);
	ledger_tree.put ("cemented_count", cemented_count);
	tree.add_child ("ledger", ledger_tree);

	// Rates, derived from counter deltas since the previous snapshot
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		auto const now = std::chrono::steady_clock::now ();
		double blocks_per_second = 0;
		double confirmations_per_second = 0;
		double messages_in_per_second = 0;
		double messages_out_per_second = 0;
		if (has_previous)
		{
			auto const elapsed = std::chrono::duration_cast<std::chrono::duration<double>> (now - last_sample).count ();
			if (elapsed > 0)
			{
				auto const delta = [elapsed] (uint64_t current, uint64_t previous) {
					return current >= previous ? (current - previous) / elapsed : 0.0;
				};
				blocks_per_second = delta (block_count, last_block_count);
				confirmations_per_second = delta (cemented_count, last_cemented_count);
				messages_in_per_second = delta (messages_in, last_messages_in);
				messages_out_per_second = delta (messages_out, last_messages_out);
			}
		}
		last_sample = now;
		last_block_count = block_count;
		last_cemented_count = cemented_count;
		last_messages_in = messages_in;
		last_messages_out = messages_out;
		has_previous = true;

		boost::property_tree::ptree rates_tree;
		rates_tree.put ("blocks_per_second", static_cast<uint64_t> (blocks_per_second));
		rates_tree.put ("confirmations_per_second", static_cast<uint64_t> (confirmations_per_second));
		rates_tree.put ("messages_in_per_second", static_cast<uint64_t> (messages_in_per_second));
		rates_tree.put ("messages_out_per_second", static_cast<uint64_t> (messages_out_per_second));
		tree.add_child ("rates", rates_tree);
	}

	// Online / trended weight
	boost::property_tree::ptree weight_tree;
	weight_tree.put ("online", weight_to_string (node.online_reps.online ()));
	weight_tree.put ("trended", weight_to_string (node.online_reps.trended ()));
	tree.add_child ("online_weight", weight_tree);

	// Peers, joined with telemetry
	auto const minimum_principal = node.minimum_principal_weight ();
	auto const telemetries = node.telemetry.get_all_telemetries ();
	boost::property_tree::ptree peers_tree;
	// Sort by remote endpoint (address, then port) so the list is stable across updates
	auto channels = node.network.list ();
	std::sort (channels.begin (), channels.end (), [] (auto const & lhs, auto const & rhs) {
		return lhs->get_remote_endpoint () < rhs->get_remote_endpoint ();
	});
	for (auto const & channel : channels)
	{
		boost::property_tree::ptree peer;
		auto const endpoint = channel->get_remote_endpoint ();
		peer.put ("endpoint", boost::lexical_cast<std::string> (endpoint));
		peer.put ("protocol_version", static_cast<int> (channel->get_network_version ()));

		auto const node_id = channel->get_node_id ();
		peer.put ("node_id", node_id.to_account ());

		auto const weight = node.ledger.weight (node_id);
		peer.put ("rep_weight", weight_to_string (weight));
		peer.put ("principal", weight >= minimum_principal);

		// Telemetry may be keyed by the remote or the peering endpoint
		auto telemetry = telemetries.find (endpoint);
		if (telemetry == telemetries.end ())
		{
			telemetry = telemetries.find (channel->get_peering_endpoint ());
		}
		if (telemetry != telemetries.end ())
		{
			auto const & data = telemetry->second;
			peer.put ("block_count", data.block_count);
			peer.put ("cemented_count", data.cemented_count);
			peer.put ("unchecked_count", data.unchecked_count);
			peer.put ("bandwidth_cap", data.bandwidth_cap);
			peer.put ("maker", nano::messages::to_string (data.maker));
			peer.put ("version", std::to_string (data.major_version) + "." + std::to_string (data.minor_version) + "." + std::to_string (data.patch_version));
		}
		peers_tree.push_back (std::make_pair ("", peer));
	}
	tree.add_child ("peers", peers_tree);

	// Representatives (online), sorted by weight descending
	boost::property_tree::ptree reps_tree;
	{
		std::vector<std::pair<nano::account, nano::uint128_t>> reps;
		for (auto const & account : node.online_reps.list ())
		{
			reps.emplace_back (account, node.ledger.weight (account));
		}
		std::sort (reps.begin (), reps.end (), [] (auto const & lhs, auto const & rhs) {
			return lhs.second > rhs.second;
		});
		for (auto const & [account, weight] : reps)
		{
			boost::property_tree::ptree rep;
			rep.put ("account", account.to_account ());
			rep.put ("weight", weight_to_string (weight));
			rep.put ("principal", weight >= minimum_principal);
			reps_tree.push_back (std::make_pair ("", rep));
		}
	}
	tree.add_child ("representatives", reps_tree);

	// Queues
	boost::property_tree::ptree queues_tree;
	{
		boost::property_tree::ptree elections;
		elections.put ("priority", node.active.size (nano::election_behavior::priority));
		elections.put ("hinted", node.active.size (nano::election_behavior::hinted));
		elections.put ("optimistic", node.active.size (nano::election_behavior::optimistic));
		elections.put ("total", node.active.size ());
		queues_tree.add_child ("active_elections", elections);

		queues_tree.put ("block_processor", node.block_processor.size ());
		queues_tree.put ("vote_processor", node.vote_processor.size ());
		queues_tree.put ("confirming", node.cementing_set.size ());
	}
	tree.add_child ("queues", queues_tree);

	return tree;
}
