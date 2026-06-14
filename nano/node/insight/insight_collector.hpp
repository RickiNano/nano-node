#pragma once

#include <nano/lib/locks.hpp>
#include <nano/node/fwd.hpp>

#include <boost/property_tree/ptree_fwd.hpp>

#include <chrono>
#include <cstdint>

namespace nano::insight
{
/**
 * Collects a point-in-time JSON snapshot of live node state for the Insight dashboard.
 *
 * This is the C++ equivalent of the RsNano `InsightApp::update` collector: it reads
 * the node subsystems (network, telemetry, ledger, active elections, queues, stats)
 * and produces a JSON tree consumed by the browser frontend.
 *
 * The collector is stateful: it remembers the previous sample so it can derive
 * per-second rates (blocks, confirmations, messages) from counter deltas, mirroring
 * the Rust `MessageRateCalculator` / `LedgerStats`.
 */
class collector final
{
public:
	explicit collector (nano::node &);

	/** Build a fresh snapshot of the current node state. Thread-safe. */
	boost::property_tree::ptree snapshot ();

private:
	nano::node & node;

	nano::mutex mutex;
	bool has_previous{ false };
	std::chrono::steady_clock::time_point last_sample;
	uint64_t last_block_count{ 0 };
	uint64_t last_cemented_count{ 0 };
	uint64_t last_messages_in{ 0 };
	uint64_t last_messages_out{ 0 };
};
}
