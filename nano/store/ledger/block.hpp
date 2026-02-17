#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/backend.hpp>
#include <nano/store/block_w_sideband.hpp>
#include <nano/store/iterator.hpp>

#include <boost/endian/conversion.hpp>

#include <cstring>
#include <functional>
#include <optional>
#include <variant>

namespace nano::store::ledger
{
class successor_view;

class block_iterator final
{
public:
	using value_type = std::pair<nano::block_hash, nano::store::block_w_sideband>;
	using iterator_category = std::bidirectional_iterator_tag;
	using pointer = value_type *;
	using const_pointer = value_type const *;
	using reference = value_type &;
	using const_reference = value_type const &;

	// Regular iterator (wraps block_index iterator, does block_data lookups)
	block_iterator (nano::store::iterator && index_iter,
					nano::store::backend const & backend,
					nano::store::transaction const & txn) noexcept;

	// End sentinel
	block_iterator (nano::store::iterator && index_iter) noexcept;

	block_iterator (block_iterator const &) = delete;
	auto operator= (block_iterator const &) -> block_iterator & = delete;
	block_iterator (block_iterator && other) noexcept;
	auto operator= (block_iterator && other) noexcept -> block_iterator &;

	auto operator++ () -> block_iterator &;
	auto operator-- () -> block_iterator &;
	auto operator->() const -> const_pointer;
	auto operator* () const -> const_reference;
	auto operator== (block_iterator const & other) const -> bool;
	bool is_end () const;

private:
	void update ();

	nano::store::iterator index_iter;
	nano::store::backend const * backend_ptr{}; // nullptr for end sentinel
	nano::store::transaction const * txn_ptr{}; // nullptr for end sentinel
	std::variant<std::monostate, value_type> current;
};

class block_view
{
public:
	using iterator = block_iterator;

public:
	block_view (nano::store::backend &, nano::store::ledger::successor_view &);

	void put (nano::store::write_transaction const &, nano::block_hash const &, nano::block const &);
	void raw_put (nano::store::write_transaction const &, std::vector<uint8_t> const & data, nano::block_hash const &);
	std::shared_ptr<nano::block> get (nano::store::transaction const &, nano::block_hash const &) const;
	void del (nano::store::write_transaction const &, nano::block_hash const &);
	bool exists (nano::store::transaction const &, nano::block_hash const &) const;
	uint64_t count (nano::store::transaction const &) const;
	iterator begin (nano::store::transaction const &, nano::block_hash const &) const;
	iterator begin (nano::store::transaction const &) const;
	iterator end (nano::store::transaction const &) const;
	void for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const;

private:
	uint64_t next_id (nano::store::transaction const &) const;

private:
	nano::store::backend & backend;
	nano::store::ledger::successor_view & successor_store;
};
}
