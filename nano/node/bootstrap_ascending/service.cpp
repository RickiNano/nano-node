#include <nano/lib/blocks.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/bootstrap_ascending/service.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/account.hpp>
#include <nano/store/component.hpp>

using namespace std::chrono_literals;

/*
 * bootstrap_ascending
 */

nano::bootstrap_ascending::service::service (nano::node_config & config_a, nano::block_processor & block_processor_a, nano::ledger & ledger_a, nano::network & network_a, nano::stats & stat_a) :
	config{ config_a },
	network_consts{ config.network_params.network },
	block_processor{ block_processor_a },
	ledger{ ledger_a },
	network{ network_a },
	stats{ stat_a },
	accounts{ stats },
	iterator{ ledger },
	throttle{ compute_throttle_size () },
	scoring{ config.bootstrap_ascending, config.network_params.network },
	database_limiter{ config.bootstrap_ascending.database_requests_limit, 1.0 }
{
	// TODO: This is called from a very congested blockprocessor thread. Offload this work to a dedicated processing thread
	block_processor.batch_processed.add ([this] (auto const & batch) {
		{
			nano::lock_guard<nano::mutex> lock{ mutex };

			auto transaction = ledger.tx_begin_read ();
			for (auto const & [result, context] : batch)
			{
				debug_assert (context.block != nullptr);
				inspect (transaction, result, *context.block);
			}
		}

		condition.notify_all ();
	});
}

nano::bootstrap_ascending::service::~service ()
{
	// All threads must be stopped before destruction
	debug_assert (!priorities_thread.joinable ());
	debug_assert (!dependencies_thread.joinable ());
	debug_assert (!timeout_thread.joinable ());
}

void nano::bootstrap_ascending::service::start ()
{
	debug_assert (!priorities_thread.joinable ());
	debug_assert (!dependencies_thread.joinable ());
	debug_assert (!timeout_thread.joinable ());

	priorities_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::ascending_bootstrap);
		run_priorities ();
	});

	dependencies_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::ascending_bootstrap);
		run_dependencies ();
	});

	timeout_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::ascending_bootstrap);
		run_timeouts ();
	});
}

void nano::bootstrap_ascending::service::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	nano::join_or_pass (priorities_thread);
	nano::join_or_pass (dependencies_thread);
	nano::join_or_pass (timeout_thread);
}

void nano::bootstrap_ascending::service::send (std::shared_ptr<nano::transport::channel> const & channel, async_tag tag)
{
	debug_assert (tag.type != query_type::invalid);

	nano::asc_pull_req request{ network_consts };
	request.id = tag.id;

	switch (tag.type)
	{
		case query_type::blocks_by_hash:
		case query_type::blocks_by_account:
		{
			request.type = nano::asc_pull_type::blocks;

			nano::asc_pull_req::blocks_payload pld;
			pld.start = tag.start;
			pld.count = config.bootstrap_ascending.pull_count;
			pld.start_type = tag.type == query_type::blocks_by_hash ? nano::asc_pull_req::hash_type::block : nano::asc_pull_req::hash_type::account;
			request.payload = pld;

			stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::request_blocks, nano::stat::dir::out);
		}
		break;
		case query_type::account_info_by_hash:
		{
			request.type = nano::asc_pull_type::account_info;

			nano::asc_pull_req::account_info_payload pld;
			pld.target_type = nano::asc_pull_req::hash_type::block; // Query account info by block hash
			pld.target = tag.start;
			request.payload = pld;

			stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::request_account_info, nano::stat::dir::out);
		}
		break;
		default:
			debug_assert (false);
	}

	request.update_header ();

	stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::request, nano::stat::dir::out);

	// TODO: There is no feedback mechanism if bandwidth limiter starts dropping our requests
	channel->send (
	request, nullptr,
	nano::transport::buffer_drop_policy::limiter, nano::transport::traffic_type::bootstrap);
}

std::size_t nano::bootstrap_ascending::service::priority_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.priority_size ();
}

std::size_t nano::bootstrap_ascending::service::blocked_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.blocked_size ();
}

std::size_t nano::bootstrap_ascending::service::score_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return scoring.size ();
}

/** Inspects a block that has been processed by the block processor
- Marks an account as blocked if the result code is gap source as there is no reason request additional blocks for this account until the dependency is resolved
- Marks an account as forwarded if it has been recently referenced by a block that has been inserted.
 */
void nano::bootstrap_ascending::service::inspect (secure::transaction const & tx, nano::block_status const & result, nano::block const & block)
{
	auto const hash = block.hash ();

	switch (result)
	{
		case nano::block_status::progress:
		{
			const auto account = block.account ();

			// If we've inserted any block in to an account, unmark it as blocked
			accounts.unblock (account);
			accounts.priority_up (account);
			accounts.timestamp (account, /* reset timestamp */ true);

			if (block.is_send ())
			{
				auto destination = block.destination ();
				accounts.unblock (destination, hash); // Unblocking automatically inserts account into priority set
				accounts.priority_up (destination);
			}
		}
		break;
		case nano::block_status::gap_source:
		{
			const auto account = block.previous ().is_zero () ? block.account_field ().value () : ledger.any.block_account (tx, block.previous ()).value ();
			const auto source = block.source_field ().value_or (block.link_field ().value_or (0).as_block_hash ());

			// Mark account as blocked because it is missing the source block
			accounts.block (account, source);

			// TODO: Track stats
		}
		break;
		case nano::block_status::old:
		{
			// TODO: Track stats
		}
		break;
		case nano::block_status::gap_previous:
		{
			// TODO: Track stats
		}
		break;
		default: // No need to handle other cases
			break;
	}
}

void nano::bootstrap_ascending::service::wait_blockprocessor ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped && block_processor.size (nano::block_source::bootstrap) > config.bootstrap_ascending.block_wait_count)
	{
		condition.wait_for (lock, std::chrono::milliseconds{ config.bootstrap_ascending.throttle_wait }, [this] () { return stopped; }); // Blockprocessor is relatively slow, sleeping here instead of using conditions
	}
}

std::shared_ptr<nano::transport::channel> nano::bootstrap_ascending::service::wait_available_channel ()
{
	std::shared_ptr<nano::transport::channel> channel;
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped && !(channel = scoring.channel ()))
	{
		condition.wait_for (lock, std::chrono::milliseconds{ config.bootstrap_ascending.throttle_wait }, [this] () { return stopped; });
	}
	return channel;
}

nano::account nano::bootstrap_ascending::service::available_account ()
{
	debug_assert (!mutex.try_lock ());
	{
		auto account = accounts.next_priority ();
		if (!account.is_zero ())
		{
			stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::next_priority);
			return account;
		}
	}
	if (database_limiter.should_pass (1))
	{
		auto account = iterator.next ();
		if (!account.is_zero ())
		{
			stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::next_database);
			return account;
		}
	}
	return { 0 };
}

nano::account nano::bootstrap_ascending::service::wait_available_account ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		auto account = available_account ();
		if (!account.is_zero ())
		{
			accounts.timestamp (account);
			return account;
		}
		else
		{
			condition.wait_for (lock, 100ms);
		}
	}
	return { 0 };
}

nano::block_hash nano::bootstrap_ascending::service::available_dependency ()
{
	debug_assert (!mutex.try_lock ());

	auto dependency = accounts.next_blocking ();
	if (!dependency.is_zero ())
	{
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::next_dependency);
		return dependency;
	}
	return { 0 };
}

nano::block_hash nano::bootstrap_ascending::service::wait_available_dependency ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		auto dependency = available_dependency ();
		if (!dependency.is_zero ())
		{
			return dependency;
		}
		else
		{
			condition.wait_for (lock, 100ms);
		}
	}
	return { 0 };
}

bool nano::bootstrap_ascending::service::request (nano::account account, std::shared_ptr<nano::transport::channel> const & channel)
{
	async_tag tag{};
	tag.id = nano::bootstrap_ascending::generate_id ();
	tag.account = account;
	tag.time = nano::milliseconds_since_epoch ();

	// Check if the account picked has blocks, if it does, start the pull from the highest block
	auto info = ledger.store.account.get (ledger.store.tx_begin_read (), account);
	if (info)
	{
		tag.type = query_type::blocks_by_hash;
		tag.start = info->head;
	}
	else
	{
		tag.type = query_type::blocks_by_account;
		tag.start = account;
	}

	on_request.notify (tag, channel);

	track (tag);
	send (channel, tag);

	return true; // Request sent
}

bool nano::bootstrap_ascending::service::request_info (nano::block_hash hash, std::shared_ptr<nano::transport::channel> const & channel)
{
	async_tag tag{};
	tag.id = generate_id ();
	tag.time = nano::milliseconds_since_epoch ();

	tag.type = query_type::account_info_by_hash;
	tag.start = hash;

	on_request.notify (tag, channel);

	track (tag);
	send (channel, tag);

	return true; // Request sent
}

void nano::bootstrap_ascending::service::throttle_if_needed (nano::unique_lock<nano::mutex> & lock) const
{
	debug_assert (lock.owns_lock ());
	if (!iterator.warmup () && throttle.throttled ())
	{
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::throttled);
		condition.wait_for (lock, std::chrono::milliseconds{ config.bootstrap_ascending.throttle_wait }, [this] () { return stopped; });
	}
}

bool nano::bootstrap_ascending::service::run_one_priority ()
{
	// Ensure there is enough space in blockprocessor for queuing new blocks
	wait_blockprocessor ();

	// Waits for channel that is not full
	auto channel = wait_available_channel ();
	if (!channel)
	{
		return false;
	}

	// Waits for account either from priority queue or database
	auto account = wait_available_account ();
	if (account.is_zero ())
	{
		return false;
	}

	bool success = request (account, channel);
	return success;
}

void nano::bootstrap_ascending::service::run_priorities ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::loop);
		run_one_priority ();
		lock.lock ();
		throttle_if_needed (lock);
	}
}

bool nano::bootstrap_ascending::service::run_one_dependency ()
{
	// Ensure there is enough space in blockprocessor for queuing new blocks
	wait_blockprocessor ();

	auto channel = wait_available_channel ();
	if (!channel)
	{
		return false;
	}

	auto dependency = wait_available_dependency ();
	if (dependency.is_zero ())
	{
		return false;
	}

	bool success = request_info (dependency, channel);
	return success;
}

void nano::bootstrap_ascending::service::run_dependencies ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::loop_dependencies);
		run_one_dependency ();
		lock.lock ();
	}
}

void nano::bootstrap_ascending::service::run_timeouts ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		scoring.sync (network.list ());
		scoring.timeout ();
		throttle.resize (compute_throttle_size ());
		auto & tags_by_order = tags.get<tag_sequenced> ();
		while (!tags_by_order.empty () && nano::time_difference (tags_by_order.front ().time, nano::milliseconds_since_epoch ()) > config.bootstrap_ascending.timeout)
		{
			auto tag = tags_by_order.front ();
			tags_by_order.pop_front ();
			on_timeout.notify (tag);
			stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::timeout);
		}
		condition.wait_for (lock, 1s, [this] () { return stopped; });
	}
}

void nano::bootstrap_ascending::service::process (nano::asc_pull_ack const & message, std::shared_ptr<nano::transport::channel> const & channel)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	// Only process messages that have a known tag
	auto & tags_by_id = tags.get<tag_id> ();
	if (tags_by_id.count (message.id) > 0)
	{
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::reply);

		auto iterator = tags_by_id.find (message.id);
		auto tag = *iterator;
		tags_by_id.erase (iterator);

		// Track bootstrap request response time
		stats.sample (nano::stat::sample::bootstrap_tag_duration, nano::milliseconds_since_epoch () - tag.time, { 0, config.bootstrap_ascending.timeout });

		scoring.received_message (channel);

		lock.unlock ();

		on_reply.notify (tag);
		condition.notify_all ();

		std::visit ([this, &tag] (auto && request) { return process (request, tag); }, message.payload);
	}
	else
	{
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::missing_tag);
	}
}

void nano::bootstrap_ascending::service::process (const nano::asc_pull_ack::blocks_payload & response, const nano::bootstrap_ascending::service::async_tag & tag)
{
	stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::process);

	auto result = verify (response, tag);
	switch (result)
	{
		case verify_result::ok:
		{
			stats.add (nano::stat::type::bootstrap_ascending, nano::stat::detail::blocks, nano::stat::dir::in, response.blocks.size ());

			for (auto & block : response.blocks)
			{
				block_processor.add (block, nano::block_source::bootstrap);
			}
			nano::lock_guard<nano::mutex> lock{ mutex };
			throttle.add (true);
		}
		break;
		case verify_result::nothing_new:
		{
			stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::nothing_new);

			nano::lock_guard<nano::mutex> lock{ mutex };
			accounts.priority_down (tag.account);
			throttle.add (false);
		}
		break;
		case verify_result::invalid:
		{
			stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::invalid);
			// TODO: Log
		}
		break;
	}
}

void nano::bootstrap_ascending::service::process (const nano::asc_pull_ack::account_info_payload & response, const nano::bootstrap_ascending::service::async_tag & tag)
{
	if (response.account.is_zero ())
	{
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::account_info_empty);
		return;
	}

	stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::account_info);

	// Prioritize account containing the dependency
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		accounts.priority_up (response.account);
	}
}

void nano::bootstrap_ascending::service::process (const nano::asc_pull_ack::frontiers_payload & response, const nano::bootstrap_ascending::service::async_tag & tag)
{
	// TODO: Make use of frontiers info
}

void nano::bootstrap_ascending::service::process (const nano::empty_payload & response, const nano::bootstrap_ascending::service::async_tag & tag)
{
	// Should not happen
	debug_assert (false, "empty payload");
}

nano::bootstrap_ascending::service::verify_result nano::bootstrap_ascending::service::verify (const nano::asc_pull_ack::blocks_payload & response, const nano::bootstrap_ascending::service::async_tag & tag) const
{
	auto const & blocks = response.blocks;

	if (blocks.empty ())
	{
		return verify_result::nothing_new;
	}
	if (blocks.size () == 1 && blocks.front ()->hash () == tag.start.as_block_hash ())
	{
		return verify_result::nothing_new;
	}

	auto const & first = blocks.front ();
	switch (tag.type)
	{
		case query_type::blocks_by_hash:
		{
			if (first->hash () != tag.start.as_block_hash ())
			{
				// TODO: Stat & log
				return verify_result::invalid;
			}
		}
		break;
		case query_type::blocks_by_account:
		{
			// Open & state blocks always contain account field
			if (first->account_field () != tag.start.as_account ())
			{
				// TODO: Stat & log
				return verify_result::invalid;
			}
		}
		break;
		default:
			return verify_result::invalid;
	}

	// Verify blocks make a valid chain
	nano::block_hash previous_hash = blocks.front ()->hash ();
	for (int n = 1; n < blocks.size (); ++n)
	{
		auto & block = blocks[n];
		if (block->previous () != previous_hash)
		{
			// TODO: Stat & log
			return verify_result::invalid; // Blocks do not make a chain
		}
		previous_hash = block->hash ();
	}

	return verify_result::ok;
}

void nano::bootstrap_ascending::service::track (async_tag const & tag)
{
	stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::track);

	nano::lock_guard<nano::mutex> lock{ mutex };
	debug_assert (tags.get<tag_id> ().count (tag.id) == 0);
	tags.get<tag_id> ().insert (tag);
}

auto nano::bootstrap_ascending::service::info () const -> nano::bootstrap_ascending::account_sets::info_t
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.info ();
}

std::size_t nano::bootstrap_ascending::service::compute_throttle_size () const
{
	// Scales logarithmically with ledger block
	// Returns: config.throttle_coefficient * sqrt(block_count)
	std::size_t size_new = config.bootstrap_ascending.throttle_coefficient * std::sqrt (ledger.block_count ());
	return size_new == 0 ? 16 : size_new;
}

std::unique_ptr<nano::container_info_component> nano::bootstrap_ascending::service::collect_container_info (std::string const & name)
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "tags", tags.size (), sizeof (decltype (tags)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "throttle", throttle.size (), 0 }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "throttle_successes", throttle.successes (), 0 }));
	composite->add_component (accounts.collect_container_info ("accounts"));
	return composite;
}
