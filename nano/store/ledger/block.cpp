#include <nano/lib/blocks.hpp>
#include <nano/lib/stream.hpp>
#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/db_val_templ.hpp>
#include <nano/store/ledger/block.hpp>
#include <nano/store/ledger/successor.hpp>

namespace nano::store::ledger
{
/*
 * block_iterator
 */

block_iterator::block_iterator (nano::store::iterator && index_iter_a,
								nano::store::backend const & backend_a,
								nano::store::transaction const & txn_a) noexcept :
	index_iter{ std::move (index_iter_a) },
	backend_ptr{ &backend_a },
	txn_ptr{ &txn_a }
{
	update ();
}

block_iterator::block_iterator (nano::store::iterator && index_iter_a) noexcept :
	index_iter{ std::move (index_iter_a) },
	backend_ptr{ nullptr },
	txn_ptr{ nullptr }
{
}

block_iterator::block_iterator (block_iterator && other) noexcept :
	index_iter{ std::move (other.index_iter) },
	backend_ptr{ other.backend_ptr },
	txn_ptr{ other.txn_ptr },
	current{ std::move (other.current) }
{
	other.backend_ptr = nullptr;
	other.txn_ptr = nullptr;
}

auto block_iterator::operator= (block_iterator && other) noexcept -> block_iterator &
{
	index_iter = std::move (other.index_iter);
	backend_ptr = other.backend_ptr;
	txn_ptr = other.txn_ptr;
	current = std::move (other.current);
	other.backend_ptr = nullptr;
	other.txn_ptr = nullptr;
	return *this;
}

auto block_iterator::operator++ () -> block_iterator &
{
	++index_iter;
	update ();
	return *this;
}

auto block_iterator::operator-- () -> block_iterator &
{
	--index_iter;
	update ();
	return *this;
}

auto block_iterator::operator->() const -> const_pointer
{
	return std::get_if<value_type> (&current);
}

auto block_iterator::operator* () const -> const_reference
{
	return std::get<value_type> (current);
}

auto block_iterator::operator== (block_iterator const & other) const -> bool
{
	return index_iter == other.index_iter;
}

bool block_iterator::is_end () const
{
	return index_iter.is_end ();
}

void block_iterator::update ()
{
	if (index_iter.is_end ())
	{
		current = std::monostate{};
		return;
	}

	debug_assert (backend_ptr != nullptr && txn_ptr != nullptr);

	auto const & [key_span, id_span] = *index_iter;

	// Read hash from key
	nano::block_hash hash;
	debug_assert (key_span.size () == sizeof (hash));
	std::memcpy (hash.bytes.data (), key_span.data (), sizeof (hash));

	// Look up block data using the ID value
	nano::store::db_val id_key{ id_span.size (), id_span.data () };
	nano::store::db_val data_val;
	auto status = backend_ptr->get (*txn_ptr, nano::store::table::block_data, id_key, data_val);
	release_assert (backend_ptr->success (status), "block_iterator: block_data lookup failed for existing block_index entry");

	// Deserialize block + sideband
	nano::bufferstream stream{ reinterpret_cast<uint8_t const *> (data_val.data ()), data_val.size () };
	nano::block_type type;
	bool error = nano::try_read (stream, type);
	release_assert (!error);
	auto block = nano::deserialize_block (stream, type);
	release_assert (block != nullptr);
	nano::block_sideband sideband;
	error = sideband.deserialize (stream, type);
	release_assert (!error);
	block->sideband_set (sideband);

	nano::store::block_w_sideband bws;
	bws.block = block;
	bws.sideband = sideband;
	current = value_type{ hash, std::move (bws) };
}

/*
 * block_view
 */

block_view::block_view (nano::store::backend & backend_a, nano::store::ledger::successor_view & successor_store_a) :
	backend{ backend_a },
	successor_store{ successor_store_a }
{
}

void block_view::put (nano::store::write_transaction const & txn, nano::block_hash const & hash, nano::block const & block)
{
	std::vector<uint8_t> vector;
	{
		nano::vectorstream stream{ vector };
		nano::serialize_block (stream, block);
		block.sideband ().serialize (stream, block.type ());
	}
	raw_put (txn, vector, hash);
	if (!block.previous ().is_zero ())
	{
		successor_store.put (txn, block.previous (), hash);
	}
	debug_assert (block.previous ().is_zero () || successor_store.get (txn, block.previous ()) == hash);
}

void block_view::raw_put (nano::store::write_transaction const & txn, std::vector<uint8_t> const & data, nano::block_hash const & hash)
{
	uint64_t id;
	// Check if hash already exists (overwrite case)
	nano::store::db_val existing_id_val;
	auto status = backend.get (txn, nano::store::table::block_index, hash, existing_id_val);
	if (backend.success (status))
	{
		id = static_cast<uint64_t> (existing_id_val);
	}
	else
	{
		id = next_id (txn);
	}
	uint64_t id_be = boost::endian::native_to_big (id);
	nano::store::db_val id_key{ sizeof (id_be), &id_be };
	// Write block_index: hash -> ID
	status = backend.put (txn, nano::store::table::block_index, hash, id_key);
	backend.release_assert_success (status);
	// Write block_data: ID -> data
	nano::store::db_val data_val{ data.size (), (void *)data.data () };
	status = backend.put (txn, nano::store::table::block_data, id_key, data_val);
	backend.release_assert_success (status);
}

std::shared_ptr<nano::block> block_view::get (nano::store::transaction const & txn, nano::block_hash const & hash) const
{
	nano::store::db_val id_val;
	auto status = backend.get (txn, nano::store::table::block_index, hash, id_val);
	if (backend.not_found (status))
	{
		return nullptr;
	}
	release_assert (backend.success (status), backend.error_string (status));

	nano::store::db_val data_val;
	status = backend.get (txn, nano::store::table::block_data, id_val, data_val);
	release_assert (backend.success (status), "block_data lookup failed for existing block_index entry");

	nano::bufferstream stream{ reinterpret_cast<uint8_t const *> (data_val.data ()), data_val.size () };
	nano::block_type type;
	bool error = nano::try_read (stream, type);
	release_assert (!error);
	auto result = nano::deserialize_block (stream, type);
	release_assert (result != nullptr);
	nano::block_sideband sideband;
	error = sideband.deserialize (stream, type);
	release_assert (!error);
	result->sideband_set (sideband);
	return result;
}

void block_view::del (nano::store::write_transaction const & txn, nano::block_hash const & hash)
{
	nano::store::db_val id_val;
	auto status = backend.get (txn, nano::store::table::block_index, hash, id_val);
	backend.release_assert_success (status);

	status = backend.del (txn, nano::store::table::block_index, hash);
	backend.release_assert_success (status);

	status = backend.del (txn, nano::store::table::block_data, id_val);
	backend.release_assert_success (status);
}

bool block_view::exists (nano::store::transaction const & txn, nano::block_hash const & hash) const
{
	return backend.exists (txn, nano::store::table::block_index, hash);
}

uint64_t block_view::count (nano::store::transaction const & txn) const
{
	return backend.count (txn, nano::store::table::block_index);
}

auto block_view::begin (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::block_index), backend, txn };
}

auto block_view::begin (nano::store::transaction const & txn, nano::block_hash const & hash) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::block_index, hash), backend, txn };
}

auto block_view::end (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, nano::store::table::block_index) };
}

void block_view::for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<nano::uint256_t> (
	[&action, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, start), !is_last ? this->begin (txn, end) : this->end (txn));
	});
}

uint64_t block_view::next_id (nano::store::transaction const & txn) const
{
	auto it = backend.end (txn, nano::store::table::block_data);
	--it; // Decrement from end to get last entry
	if (it.is_end ())
	{
		return 1; // Empty table, start at 1
	}
	auto const & [key_span, value_span] = *it;
	uint64_t max_id_be;
	std::memcpy (&max_id_be, key_span.data (), sizeof (max_id_be));
	return boost::endian::big_to_native (max_id_be) + 1;
}

}
