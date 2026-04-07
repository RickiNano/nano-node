#include <nano/lib/assert.hpp>
#include <nano/store/lmdb/common.hpp>
#include <nano/store/lmdb/lmdb_env.hpp>
#include <nano/store/lmdb/transaction_lmdb.hpp>

/*
 * read_transaction_impl
 */

nano::store::lmdb::read_transaction_impl::read_transaction_impl (nano::store::lmdb::env const & env_a, nano::store::txn_callbacks txn_callbacks_a) :
	nano::store::read_transaction_impl{ env_a.store_id },
	env{ env_a },
	txn_callbacks{ txn_callbacks_a }
{
	renew ();
}

nano::store::lmdb::read_transaction_impl::~read_transaction_impl ()
{
	// mdb_txn_abort frees the handle whether it is active or has been reset
	if (handle != nullptr)
	{
		if (active)
		{
			txn_callbacks.txn_end (this);
		}
		mdb_txn_abort (handle);
	}
}

void nano::store::lmdb::read_transaction_impl::reset ()
{
	if (active)
	{
		// Release the reader slot without freeing the MDB_txn struct so it can
		// be cheaply re-acquired in renew() via mdb_txn_renew instead of a full
		// mdb_txn_begin / mdb_txn_abort cycle.
		mdb_txn_reset (handle);
		txn_callbacks.txn_end (this);
		active = false;
	}
}

void nano::store::lmdb::read_transaction_impl::renew ()
{
	release_assert (!active);
	int status;
	if (handle != nullptr)
	{
		status = mdb_txn_renew (handle);
	}
	else
	{
		status = mdb_txn_begin (env, nullptr, MDB_RDONLY, &handle);
	}
	release_assert (success (status), error_string (status));
	txn_callbacks.txn_start (this);
	active = true;
}

void * nano::store::lmdb::read_transaction_impl::get_handle () const
{
	return handle;
}

/*
 * write_transaction_impl
 */

nano::store::lmdb::write_transaction_impl::write_transaction_impl (nano::store::lmdb::env const & env_a, nano::store::txn_callbacks txn_callbacks_a) :
	nano::store::write_transaction_impl (env_a.store_id),
	env{ env_a },
	txn_callbacks{ txn_callbacks_a }
{
	renew ();
}

nano::store::lmdb::write_transaction_impl::~write_transaction_impl ()
{
	commit ();
}

void nano::store::lmdb::write_transaction_impl::commit ()
{
	if (active)
	{
		auto status = mdb_txn_commit (handle);
		release_assert (success (status), "Unable to write to the LMDB database", error_string (status));
		handle = nullptr;
		txn_callbacks.txn_end (this);
		active = false;
	}
}

void nano::store::lmdb::write_transaction_impl::renew ()
{
	release_assert (!active);
	release_assert (handle == nullptr);
	auto status (mdb_txn_begin (env, nullptr, 0, &handle));
	release_assert (success (status), error_string (status));
	txn_callbacks.txn_start (this);
	active = true;
}

void * nano::store::lmdb::write_transaction_impl::get_handle () const
{
	return handle;
}

bool nano::store::lmdb::write_transaction_impl::contains (nano::store::table table) const
{
	// LMDB locks on every write
	return true;
}
