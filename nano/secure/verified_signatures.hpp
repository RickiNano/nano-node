#pragma once

#include <nano/lib/assert.hpp>
#include <nano/lib/numbers.hpp>

#include <array>
#include <optional>
#include <utility>

namespace nano
{
/**
 * Signature checks already performed for a block, ahead of `nano::ledger::process`.
 *
 * Verifying signatures inside the ledger write transaction holds the global write lock (shared with cementing, pruning, ...)
 * for the duration of the ed25519 work, so the block processor verifies them beforehand and passes the answers in here.
 *
 * Which key the ledger checks against is not always knowable up front: legacy blocks derive the signer from the previous
 * block, and a state block carrying an epoch link is signed either by its own account or by the epoch signer depending on
 * the previous block's balance. Rather than predicting that choice, the pre-pass records the answer for every key the
 * ledger could settle on, and the ledger looks up the one it actually picked.
 *
 * A lookup miss - including the empty set that every caller which does not pre-verify supplies - makes the ledger verify
 * inline as before, so this is always safe to omit and safe to fill in only partially.
 */
class verified_signatures final
{
public:
	// A block has at most two candidate signers: its own account and the epoch signer
	static constexpr size_t max_size{ 2 };

	void add (nano::account const & signer, bool valid)
	{
		debug_assert (size_m < max_size);
		if (size_m < max_size)
		{
			entries[size_m++] = { signer, valid };
		}
	}

	/// @returns whether the signature is valid for `signer`, or nullopt when it was not checked against that key
	std::optional<bool> lookup (nano::account const & signer) const
	{
		for (size_t i = 0; i < size_m; ++i)
		{
			if (entries[i].first == signer)
			{
				return entries[i].second;
			}
		}
		return std::nullopt;
	}

	bool empty () const
	{
		return size_m == 0;
	}

	size_t size () const
	{
		return size_m;
	}

private:
	std::array<std::pair<nano::account, bool>, max_size> entries{};
	size_t size_m{ 0 };
};
}
