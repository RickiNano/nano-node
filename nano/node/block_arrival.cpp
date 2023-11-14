#include <nano/node/block_arrival.hpp>

bool nano::block_arrival::add(nano::block_hash const & hash_a)
{
	auto now = std::chrono::steady_clock::now ();

	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		// Use emplace instead of emplace_back to avoid extra copy/move operations
		auto inserted = arrival.get<tag_sequence> ().emplace (now, hash_a);
		if (!inserted.second)
		{
			return false;
		}
	} // Release lock as soon as possible

	prune (now); // Prune outside the lock to reduce lock contention
	return true;
}

bool nano::block_arrival::recent (nano::block_hash const & hash_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto now = std::chrono::steady_clock::now ();

	prune (now); // Move prune logic into its own function

	return arrival.get<tag_hash> ().find (hash_a) != arrival.get<tag_hash> ().end ();
}

void nano::block_arrival::prune (std::chrono::steady_clock::time_point const & now)
{
	// This function assumes it's being called with mutex already locked
	while (arrival.size () > arrival_size_min && arrival.get<tag_sequence> ().front ().arrival + arrival_time_min < now)
	{
		arrival.get<tag_sequence> ().pop_front ();
	}
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (block_arrival & block_arrival, std::string const & name)
{
	std::size_t count;
	decltype (block_arrival.arrival)::value_type::size_type sizeof_element;

	{
		nano::lock_guard<nano::mutex> guard{ block_arrival.mutex };
		count = block_arrival.arrival.size ();
		sizeof_element = sizeof (decltype (block_arrival.arrival)::value_type);
	} // Minimize the scope of the lock

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "arrival", count, sizeof_element }));
	return composite;
}