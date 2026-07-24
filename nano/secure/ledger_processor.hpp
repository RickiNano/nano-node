#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/fwd.hpp>
#include <nano/secure/verified_signatures.hpp>

namespace nano
{
class ledger_processor final : public nano::mutable_block_visitor
{
public:
	ledger_processor (nano::secure::write_transaction const &, nano::ledger &, nano::verified_signatures const & verified = {});

	void send_block (nano::send_block &) override;
	void receive_block (nano::receive_block &) override;
	void open_block (nano::open_block &) override;
	void change_block (nano::change_block &) override;
	void state_block (nano::state_block &) override;

	void state_block_impl (nano::state_block &);
	void epoch_block_impl (nano::state_block &);

	nano::secure::write_transaction const & transaction;
	nano::ledger & ledger;
	nano::block_status result{ nano::block_status::invalid };

private:
	// Held by value: callers routinely pass a temporary, and it is small enough that copying is cheaper than reasoning about its lifetime
	nano::verified_signatures const verified;

	/// Checks the block signature against `signer`, reusing the pre-pass result when one covers that key
	/// @returns true on error (bad signature), matching `nano::validate_message`
	bool validate_signature (nano::account const & signer, nano::block_hash const & hash, nano::signature const & signature) const;

	bool validate_epoch_block (nano::state_block const & block);

	// Returns 1 + max(deps' topo_height) or 0 (unindexed sentinel) when the index is disabled or any dependency is itself unindexed
	uint64_t topology_height (std::shared_ptr<nano::block> const & dep1, std::shared_ptr<nano::block> const & dep2 = nullptr) const;
};
}