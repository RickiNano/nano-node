#include <nano/node/insight/insight_collector.hpp>
#include <nano/node/node.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/property_tree/ptree.hpp>

using namespace std::chrono_literals;

// The snapshot should contain all the top-level sections the dashboard renders.
TEST (insight_collector, snapshot_has_sections)
{
	nano::test::system system{ 1 };
	auto node = system.nodes[0];

	nano::insight::collector collector{ *node };
	auto tree = collector.snapshot ();

	ASSERT_TRUE (tree.get_child_optional ("ledger"));
	ASSERT_TRUE (tree.get_child_optional ("rates"));
	ASSERT_TRUE (tree.get_child_optional ("online_weight"));
	ASSERT_TRUE (tree.get_child_optional ("peers"));
	ASSERT_TRUE (tree.get_child_optional ("representatives"));
	ASSERT_TRUE (tree.get_child_optional ("queues"));

	// Ledger totals are always present and parseable
	ASSERT_GE (tree.get<uint64_t> ("ledger.block_count"), 1);

	// Queue sizes are present
	ASSERT_TRUE (tree.get_child_optional ("queues.active_elections"));
	ASSERT_NO_THROW (tree.get<uint64_t> ("queues.block_processor"));
	ASSERT_NO_THROW (tree.get<uint64_t> ("queues.vote_processor"));
}

// The first snapshot has no previous sample, so rates are zero; a later snapshot
// still produces a valid (non-negative) rate tree.
TEST (insight_collector, rates_available_after_two_samples)
{
	nano::test::system system{ 1 };
	auto node = system.nodes[0];

	nano::insight::collector collector{ *node };
	auto first = collector.snapshot ();
	ASSERT_EQ (0, first.get<uint64_t> ("rates.blocks_per_second"));

	auto second = collector.snapshot ();
	ASSERT_NO_THROW (second.get<uint64_t> ("rates.blocks_per_second"));
	ASSERT_NO_THROW (second.get<uint64_t> ("rates.messages_in_per_second"));
}
