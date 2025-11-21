# Nano Node Ledger Storage

## Overview

The Nano node implements a flexible storage layer that supports two database backends: **LMDB** (Lightning Memory-Mapped Database) and **RocksDB**. Both backends share a common abstraction layer, allowing the node to use either backend with identical functionality and data structures.

### Storage Backends

| Backend | Type | Storage Location | Size | Best For |
|---------|------|------------------|------|----------|
| **LMDB** | Memory-mapped B+ tree | `data.ldb` | Baseline | RAM-rich systems, simple deployment |
| **RocksDB** | LSM-tree | `rocksdb/` directory | ~65% of LMDB | SSD storage, space efficiency |

### Key Features

- **Backend Independence**: Identical data structures and API across both backends
- **ACID Transactions**: Both backends provide atomic, consistent, isolated, durable operations
- **Type Safety**: Strongly-typed interfaces for all operations
- **Migration Support**: Built-in tools to migrate from LMDB to RocksDB
- **Automatic Upgrades**: Database schema upgrades handled automatically

## Database Tables

The storage layer consists of **11 tables** (called "databases" in LMDB and "column families" in RocksDB):

| Table | Purpose | Key Type | Value Type |
|-------|---------|----------|------------|
| `accounts` | Account information | `account` | `account_info` |
| `blocks` | All blocks with metadata | `block_hash` | Block + `block_sideband` |
| `pending` | Receivable transactions | `pending_key` | `pending_info` |
| `confirmation_height` | Confirmation tracking | `account` | `confirmation_height_info` |
| `rep_weights` | Representative weights | `account` | `uint128_t` |
| `online_weight` | Historical voting weight | `uint64_t` (timestamp) | `amount` |
| `pruned` | Pruned block hashes | `block_hash` | Empty |
| `peers` | Known network peers | `endpoint_key` | `millis_t` (timestamp) |
| `final_votes` | Election final votes | `qualified_root` | `block_hash` |
| `meta` | Database metadata | Various | Version info |
| `vote` | (Legacy - unused) | - | - |

## Table Specifications

### accounts

**Purpose**: Stores information for all accounts that have been opened (have at least one block).

**Key**: `nano::account` (256-bit account address)
- Binary representation of the account public key
- 32 bytes

**Value**: `nano::account_info`
```cpp
struct account_info {
    block_hash head;           // Hash of the head/frontier block (most recent)
    account representative;    // Current representative for voting
    block_hash open_block;    // Hash of the first block (open block)
    amount balance;           // Current account balance (128-bit)
    uint64_t modified;        // Last modification timestamp (seconds since epoch)
    uint64_t block_count;     // Number of blocks in account chain
    epoch epoch;              // Current epoch version (epoch_0, epoch_1, epoch_2)
};
```

**Size**: Approximately 113 bytes per entry

**Operations**:
- `put()` - Insert or update account info
- `get()` - Retrieve account info by address
- `del()` - Delete account (used during rollback)
- `exists()` - Check if account exists
- `begin()` / `end()` - Iterate all accounts
- `for_each_par()` - Parallel iteration

**Implementation Files**:
- Interface: `nano/store/account.hpp`
- LMDB: `nano/store/lmdb/account.cpp`
- RocksDB: `nano/store/rocksdb/account.cpp`

**Usage Notes**:
- Only accounts with open blocks are stored
- Updated on every block confirmation
- Critical for balance lookups and account state

---

### blocks

**Purpose**: Stores all blocks in the ledger with associated sideband metadata.

**Key**: `nano::block_hash` (256-bit hash)
- Blake2b hash of the block
- 32 bytes

**Value**: Serialized block data + `nano::block_sideband`

**Block Types**:
- **State Block** (current): Universal block type supporting all operations
- **Send Block** (legacy): Transfer funds to another account
- **Receive Block** (legacy): Receive pending funds
- **Open Block** (legacy): Open new account
- **Change Block** (legacy): Change representative

**Sideband Structure**:
```cpp
struct block_sideband {
    block_hash successor;      // Next block hash (for rollback traversal)
    account account;          // Account this block belongs to
    amount balance;           // Balance after this block executes
    uint64_t height;          // Block height in account chain (1-indexed)
    uint64_t timestamp;       // Block arrival timestamp
    block_details details;    // Flags: epoch, is_send, is_receive, is_epoch
    epoch source_epoch;       // Epoch of source block (for receives)
};
```

**Serialization Format**:
```
[Block Type (1 byte)]
[Block Data (varies by type)]
[Sideband Data (fixed size)]
```

**Size**: Approximately 200-250 bytes per block (varies by type)

**Operations**:
- `put()` - Store block with sideband
- `get()` - Retrieve block by hash
- `get_no_sideband()` - Retrieve only block data
- `del()` - Delete block (pruning)
- `exists()` - Check if block exists
- `begin()` / `end()` - Iterate all blocks
- `for_each_par()` - Parallel iteration

**Implementation Files**:
- Interface: `nano/store/block.hpp`
- LMDB: `nano/store/lmdb/block.cpp`
- RocksDB: `nano/store/rocksdb/block.cpp`

**Usage Notes**:
- Largest table by far (millions to billions of entries)
- Sideband enables efficient reverse traversal without full account chain scan
- Can be pruned after confirmation to save space
- RocksDB uses tombstone flushing (every 25k deletes) for pruning performance

---

### pending

**Purpose**: Stores receivable transactions (send blocks that haven't been received yet).

**Key**: `nano::pending_key` (compound key)
```cpp
struct pending_key {
    account account;      // Destination account (32 bytes)
    block_hash hash;     // Send block hash (32 bytes)
};
```
- Total key size: 64 bytes
- Sorted first by account, then by hash
- Enables efficient lookup of all pending for an account

**Value**: `nano::pending_info`
```cpp
struct pending_info {
    account source;     // Account that sent the funds
    amount amount;      // Amount receivable (128-bit)
    epoch epoch;        // Epoch of the send block
};
```

**Size**: Approximately 65 bytes per entry

**Operations**:
- `put()` - Add pending transaction
- `get()` - Retrieve pending info
- `del()` - Remove after receive
- `exists()` - Check if pending exists
- `begin()` - Start iteration from account
- `for_each_par()` - Parallel iteration

**Implementation Files**:
- Interface: `nano/store/pending.hpp`
- LMDB: `nano/store/lmdb/pending.cpp`
- RocksDB: `nano/store/rocksdb/pending.cpp`

**Usage Notes**:
- Automatically removed when receive block is confirmed
- Critical for wallet functionality (showing receivable balance)
- Account grouping enables efficient "list all pending for account" queries
- RocksDB uses tombstone flushing for performance

---

### confirmation_height

**Purpose**: Tracks the confirmation height for each account (height of the highest confirmed block).

**Key**: `nano::account` (256-bit account address)
- 32 bytes

**Value**: `nano::confirmation_height_info`
```cpp
struct confirmation_height_info {
    uint64_t height;        // Height of confirmed frontier
    block_hash frontier;    // Hash of highest confirmed block
};
```

**Size**: Approximately 40 bytes per entry

**Operations**:
- `put()` - Update confirmation height
- `get()` - Retrieve confirmation info
- `del()` - Delete (rarely used)
- `exists()` - Check if exists
- `begin()` / `end()` - Iterate all
- `for_each_par()` - Parallel iteration

**Implementation Files**:
- Interface: `nano/store/confirmation_height.hpp`
- LMDB: `nano/store/lmdb/confirmation_height.cpp`
- RocksDB: `nano/store/rocksdb/confirmation_height.cpp`

**Usage Notes**:
- Used by confirmation height processor
- Enables fast determination of confirmed vs unconfirmed blocks
- Updated incrementally as blocks are confirmed
- Separate from accounts table for performance reasons

---

### rep_weights

**Purpose**: Lookup table of all representatives and their total voting weight.

**Key**: `nano::account` (representative account address)
- 32 bytes

**Value**: `nano::uint128_t` (total voting weight)
- 16 bytes
- Sum of all account balances delegating to this representative

**Size**: 48 bytes per entry

**Operations**:
- `put()` - Update representative weight
- `get()` - Retrieve weight
- `del()` - Remove representative (zero weight)
- `exists()` - Check if representative has weight
- `begin()` / `end()` - Iterate all representatives
- `for_each_par()` - Parallel iteration

**Implementation Files**:
- Interface: `nano/store/rep_weight.hpp`
- LMDB: `nano/store/lmdb/rep_weight.cpp`
- RocksDB: `nano/store/rocksdb/rep_weight.cpp`

**Version**: Added in database version 23

**Usage Notes**:
- Enables O(1) lookup of representative weights
- Updated when accounts change representatives or balances
- Critical for voting weight calculations
- Much smaller than accounts table (only unique representatives)

---

### online_weight

**Purpose**: Stores historical samples of online voting weight for quorum calculations.

**Key**: `uint64_t` (timestamp in seconds since epoch)
- 8 bytes

**Value**: `nano::amount` (online weight at that time)
- 16 bytes (128-bit amount)

**Size**: 24 bytes per entry

**Operations**:
- `put()` - Record weight sample
- `get()` - Retrieve weight at timestamp
- `del()` - Remove old samples
- `begin()` / `end()` - Iterate chronologically
- `for_each_par()` - Parallel iteration

**Implementation Files**:
- Interface: `nano/store/online_weight.hpp`
- LMDB: `nano/store/lmdb/online_weight.cpp`
- RocksDB: `nano/store/rocksdb/online_weight.cpp`

**Usage Notes**:
- Samples taken periodically (default: every hour)
- Used to calculate trended online weight
- Old samples are pruned (typically keep 2 weeks)
- Enables accurate quorum calculations

---

### pruned

**Purpose**: Set of pruned block hashes (blocks removed from storage to save space).

**Key**: `nano::block_hash` (256-bit hash of pruned block)
- 32 bytes

**Value**: Empty (null pointer)
- Only tracks existence

**Size**: 32 bytes per entry

**Operations**:
- `put()` - Mark block as pruned
- `exists()` - Check if block is pruned
- `del()` - Remove prune marker (rare)
- `begin()` / `end()` - Iterate all pruned
- `count()` - Count pruned blocks

**Implementation Files**:
- Interface: `nano/store/pruned.hpp`
- LMDB: `nano/store/lmdb/pruned.cpp`
- RocksDB: `nano/store/rocksdb/pruned.cpp`

**Usage Notes**:
- Enables node to know a block existed even after deletion
- Prevents re-downloading pruned blocks
- Used with deep block pruning feature
- Must keep sideband in blocks table even when pruning block data

---

### peers

**Purpose**: Stores known network peer endpoints and last contact time.

**Key**: `nano::endpoint_key` (IP address + port)
```cpp
struct endpoint_key {
    boost::asio::ip::address address;  // IPv4 or IPv6
    uint16_t port;                      // Network port
};
```
- Size varies: IPv4 = 6 bytes, IPv6 = 18 bytes

**Value**: `nano::millis_t` (timestamp of last contact)
- 8 bytes (milliseconds since epoch)

**Size**: 14-26 bytes per entry

**Operations**:
- `put()` - Add or update peer
- `get()` - Retrieve last contact time
- `del()` - Remove peer
- `exists()` - Check if peer exists
- `begin()` / `end()` - Iterate all peers
- `clear()` - Remove all peers

**Implementation Files**:
- Interface: `nano/store/peer.hpp`
- LMDB: `nano/store/lmdb/peer.cpp`
- RocksDB: `nano/store/rocksdb/peer.cpp`

**Usage Notes**:
- Used for peer discovery and network connectivity
- Persists across node restarts
- Stale peers are periodically removed
- Not critical for consensus (can be cleared safely)

---

### final_votes

**Purpose**: Stores final votes for election resolution (vote cache).

**Key**: `nano::qualified_root` (account + root combination)
```cpp
struct qualified_root {
    block_hash previous;  // Previous block hash
    block_hash root;      // Root hash (previous or link)
};
```
- 64 bytes

**Value**: `nano::block_hash` (winning block hash)
- 32 bytes

**Size**: 96 bytes per entry

**Operations**:
- `put()` - Record final vote
- `get()` - Retrieve final vote
- `del()` - Remove entry
- `exists()` - Check if final vote exists
- `begin()` / `end()` - Iterate all
- `clear()` - Remove all (maintenance)

**Implementation Files**:
- Interface: `nano/store/final_vote.hpp`
- LMDB: `nano/store/lmdb/final_vote.cpp`
- RocksDB: `nano/store/rocksdb/final_vote.cpp`

**Usage Notes**:
- Caches election results
- Helps resolve forks quickly on restart
- Can be cleared safely (will be rebuilt)
- Used by active elections system

---

### meta

**Purpose**: Stores database version and metadata.

**Key**: Various metadata keys (implementation-specific)
- Version key: Single byte identifier

**Value**: Version numbers and configuration
- Version: 4-byte integer

**Operations**:
- `put()` - Update metadata
- `get()` - Retrieve metadata
- Version retrieval functions

**Implementation Files**:
- Interface: `nano/store/version.hpp`
- LMDB: `nano/store/lmdb/lmdb.cpp`
- RocksDB: `nano/store/rocksdb/rocksdb.cpp`

**Current Version**: 24

**Version History**:
- Version 21: Minimum supported version
- Version 22: Removed unchecked table
- Version 23: Added rep_weights table
- Version 24: Removed frontiers table (current)

**Usage Notes**:
- Checked on node startup
- Triggers automatic upgrades
- Prevents running old software on new databases

---

## Key/Value Serialization

### Binary Format

All keys and values use binary serialization for compact storage and fast access:

- **Endianness**: Big-endian (network byte order)
- **Fixed-size types**: Direct binary representation
- **Variable-size types**: Length-prefixed
- **Compound types**: Sequential field serialization

### Serialization Methods

```cpp
// Stream-based serialization
void serialize (nano::stream & stream) const;
void deserialize (nano::stream & stream);

// Buffer-based serialization
std::vector<uint8_t> serialize () const;
bool deserialize (std::span<uint8_t const> buffer);
```

### db_val Abstraction

The `nano::db_val` class provides type-safe conversion:

```cpp
class db_val {
    std::span<uint8_t const> buffer;  // Raw bytes

    // Constructors from various types
    db_val (uint64_t value);
    db_val (nano::amount const & value);
    db_val (nano::account const & value);
    db_val (nano::block_hash const & value);

    // Conversion to types
    uint64_t as_uint64 () const;
    nano::amount as_amount () const;
    nano::account as_account () const;
    nano::block_hash as_hash () const;
};
```

**Implementation Files**:
- `nano/store/db_val.hpp`
- `nano/store/db_val.cpp`

---

## Transaction Model

Both backends provide ACID transaction semantics with a common interface.

### Transaction Types

#### Read Transactions

**Purpose**: Query the database without modification

**Characteristics**:
- Multiple concurrent readers allowed
- Snapshot isolation (consistent view)
- Can be long-lived
- No locks between readers

**Interface**:
```cpp
class read_transaction {
    void reset ();              // Reset to initial state
    void renew ();             // Renew for reuse
    void refresh ();           // Commit and start new
    void refresh_if_needed (std::chrono::milliseconds age);
};
```

#### Write Transactions

**Purpose**: Modify the database

**Characteristics**:
- Single writer at a time (per backend locking)
- Automatic commit on destructor (RAII)
- Can be explicitly committed
- All-or-nothing atomicity

**Interface**:
```cpp
class write_transaction {
    void commit ();            // Explicit commit
    void renew ();            // Renew for reuse (post-commit)
    void refresh ();          // Commit and start new
    bool contains (tables table); // Check if table was modified
};
```

### LMDB Transactions

**Implementation**: `nano/store/lmdb/transaction_impl.hpp`

- **Read**: MDB_txn in MDB_RDONLY mode
  - No locking between readers
  - Can be reset/renewed for long-lived transactions
  - Very lightweight

- **Write**: MDB_txn in read-write mode
  - Single writer enforced by LMDB
  - Mutex protection at application level
  - Automatic commit in destructor

**Transaction Tracking**:
- Long-running transaction monitoring
- Configurable timeout warnings
- Thread ID tracking

### RocksDB Transactions

**Implementation**: `nano/store/rocksdb/transaction_impl.hpp`

- **Read**: ::rocksdb::ReadOptions
  - Snapshot-based consistency
  - No actual transaction object (just options)
  - Very lightweight

- **Write**: ::rocksdb::Transaction
  - Optimistic concurrency control
  - Automatic commit in destructor
  - Conflict detection on commit

**Features**:
- TransactionDB for ACID guarantees
- Write-ahead log (WAL) for durability
- Automatic rollback on failures

### Common Usage Patterns

```cpp
// Read transaction
{
    auto tx = store.tx_begin_read ();
    auto account = store.account.get (tx, address);
    auto block = store.block.get (tx, hash);
    // Automatic cleanup on scope exit
}

// Write transaction
{
    auto tx = store.tx_begin_write ();
    store.account.put (tx, address, info);
    store.block.put (tx, hash, block, sideband);
    tx.commit ();  // Explicit commit (or automatic on destructor)
}

// Long-lived read transaction
auto tx = store.tx_begin_read ();
while (processing) {
    // Do work
    tx.refresh_if_needed (500ms);  // Refresh if older than 500ms
}
```

---

## LMDB Backend

### Architecture

**Type**: Memory-mapped B+ tree database

**File**: `data.ldb` (single file)

**Implementation Files**:
- `nano/store/lmdb/lmdb.hpp`
- `nano/store/lmdb/lmdb.cpp`
- Individual table implementations in `nano/store/lmdb/*.cpp`

### Features

- **Memory Mapping**: File is mmap'd into process address space
- **Copy-on-Write**: B+ tree modifications create new pages
- **Zero-Copy Reads**: Data accessed directly from memory map
- **ACID Transactions**: Full durability guarantees
- **Multiple Databases**: Each table is a separate named database (DBI)

### Configuration

```cpp
struct lmdb_config {
    sync_strategy sync = sync_strategy::always;
    uint32_t max_databases = 128;
    size_t map_size = 256ULL * 1024 * 1024 * 1024;  // 256 GB
};
```

**Sync Strategies**:
- `always`: Flush on every commit (safest, slowest)
- `nosync_safe`: Delay metadata flush (faster, maintains integrity)
- `nosync_unsafe`: Let OS decide (fastest, risk of corruption on crash)
- `nosync_unsafe_large_memory`: Writable mmap (for in-memory databases)

**TOML Configuration**:
```toml
[node.lmdb]
sync = "always"              # or "nosync_safe", "nosync_unsafe"
max_databases = 128
map_size = 274877906944      # 256 GB in bytes
```

### Database Handles

LMDB uses DBI (Database Handle) for each table:

```cpp
MDB_dbi accounts_v0_handle;
MDB_dbi blocks_handle;
MDB_dbi pending_v0_handle;
MDB_dbi confirmation_height_handle;
MDB_dbi rep_weights_handle;
MDB_dbi online_weight_handle;
MDB_dbi pruned_handle;
MDB_dbi peers_handle;
MDB_dbi final_votes_handle;
MDB_dbi meta_handle;
```

### Operations

**Open Database**:
```cpp
MDB_env * env;
mdb_env_create (&env);
mdb_env_set_maxdbs (env, max_databases);
mdb_env_set_mapsize (env, map_size);
mdb_env_open (env, path, flags, 0600);
```

**Create Table**:
```cpp
MDB_txn * txn;
mdb_txn_begin (env, nullptr, 0, &txn);
mdb_dbi_open (txn, "accounts", MDB_CREATE, &accounts_handle);
mdb_txn_commit (txn);
```

**Read Operation**:
```cpp
MDB_txn * txn;
mdb_txn_begin (env, nullptr, MDB_RDONLY, &txn);

MDB_val key, value;
key.mv_data = account_bytes;
key.mv_size = 32;

int result = mdb_get (txn, accounts_handle, &key, &value);
if (result == 0) {
    // value.mv_data contains account_info
}

mdb_txn_abort (txn);
```

**Write Operation**:
```cpp
MDB_txn * txn;
mdb_txn_begin (env, nullptr, 0, &txn);

MDB_val key, value;
key.mv_data = account_bytes;
key.mv_size = 32;
value.mv_data = account_info_bytes;
value.mv_size = sizeof(account_info);

mdb_put (txn, accounts_handle, &key, &value, 0);
mdb_txn_commit (txn);
```

### Maintenance

**Vacuum**: Compacts database file
```bash
nano_node --vacuum
```

**Copy**: Creates online backup
```cpp
mdb_env_copy (env, backup_path);
```

**Statistics**:
```cpp
MDB_stat stat;
mdb_stat (txn, dbi, &stat);
// stat.ms_entries = number of entries
```

### Performance Characteristics

**Advantages**:
- Extremely fast reads (zero-copy)
- Simple deployment (single file)
- Mature and stable
- Well-tested in Nano

**Disadvantages**:
- Larger disk footprint
- Slower writes (B+ tree updates)
- Map size must be pre-allocated
- Can fragment over time (need vacuum)

---

## RocksDB Backend

### Architecture

**Type**: LSM-tree (Log-Structured Merge-tree) database

**Directory**: `rocksdb/` (multiple files)

**Implementation Files**:
- `nano/store/rocksdb/rocksdb.hpp`
- `nano/store/rocksdb/rocksdb.cpp`
- Individual table implementations in `nano/store/rocksdb/*.cpp`

### Features

- **LSM-Tree**: Write-optimized data structure
- **Column Families**: Each table is a column family
- **Compaction**: Background merging of sorted files
- **Compression**: Optional compression (currently disabled)
- **WAL**: Write-ahead log for crash recovery
- **Block Cache**: Configurable read cache

### Configuration

```cpp
struct rocksdb_config {
    bool enable = false;
    unsigned io_threads = std::thread::hardware_concurrency () / 2;
    long read_cache = 32 * 1024 * 1024;   // 32 MB
    long write_cache = 64 * 1024 * 1024;  // 64 MB per column family
};
```

**TOML Configuration**:
```toml
[node.rocksdb]
enable = true
io_threads = 8              # Background compaction threads
read_cache = 33554432       # 32 MB block cache
write_cache = 67108864      # 64 MB memtable size
```

### RocksDB Options

**DBOptions**:
```cpp
::rocksdb::Options options;
options.create_if_missing = true;
options.create_missing_column_families = true;
options.max_background_jobs = io_threads;
options.max_background_compactions = io_threads;
options.bytes_per_sync = 256 * 1024;  // 256 KB
options.wal_bytes_per_sync = 256 * 1024;
```

**TableOptions**:
```cpp
::rocksdb::BlockBasedTableOptions table_options;
table_options.block_cache = NewLRUCache (read_cache);
table_options.filter_policy = NewBloomFilterPolicy (10);  // 1% false positive
table_options.index_type = IndexType::kHashSearch;
table_options.format_version = 5;
```

**ColumnFamilyOptions**:
```cpp
::rocksdb::ColumnFamilyOptions cf_options;
cf_options.write_buffer_size = write_cache;
cf_options.max_write_buffer_number = 4;
cf_options.level_compaction_dynamic_level_bytes = true;
cf_options.compression = ::rocksdb::CompressionType::kNoCompression;
```

### Column Families

Each table is a column family:

```cpp
std::vector<::rocksdb::ColumnFamilyHandle *> handles;

// Column families (order matters for handle assignment):
"default"              // Unused (required by RocksDB)
"accounts"
"blocks"
"confirmation_height"
"final_votes"
"meta"
"online_weight"
"peers"
"pending"
"pruned"
"rep_weights"
```

### Operations

**Open Database**:
```cpp
::rocksdb::TransactionDB * db;
::rocksdb::TransactionDBOptions txn_db_options;
std::vector<::rocksdb::ColumnFamilyDescriptor> descriptors;

// Create descriptors for each column family
descriptors.push_back ({"accounts", cf_options});
descriptors.push_back ({"blocks", cf_options});
// ... etc

::rocksdb::TransactionDB::Open (options, txn_db_options, path,
                                descriptors, &handles, &db);
```

**Read Operation**:
```cpp
::rocksdb::ReadOptions read_options;
std::string value;

auto status = db->Get (read_options, handles[ACCOUNTS], key, &value);
if (status.ok ()) {
    // value contains account_info bytes
}
```

**Write Operation**:
```cpp
::rocksdb::WriteOptions write_options;
::rocksdb::Transaction * txn = db->BeginTransaction (write_options);

txn->Put (handles[ACCOUNTS], key, value);
txn->Commit ();

delete txn;
```

### Tombstone Flushing

RocksDB tracks deletions and triggers flushes to improve performance:

```cpp
// Flush memtables when many deletions occur
constexpr int tombstone_flush_threshold = 25000;

// Applied to:
- blocks table
- accounts table
- pending table

// Triggers compaction to remove tombstones
```

### Maintenance

**Compaction**: Runs automatically in background
- Merges sorted files
- Removes deleted entries
- Applies in level-order

**Backup**:
```cpp
::rocksdb::BackupEngine * backup_engine;
CreateBackupEngine (env, backup_options, &backup_engine);
backup_engine->CreateNewBackup (db);
```

**Statistics**:
```cpp
uint64_t count;
db->GetIntProperty (handle, "rocksdb.estimate-num-keys", &count);
```

### Performance Characteristics

**Advantages**:
- Smaller disk footprint (~65% of LMDB)
- Better write performance
- No map size pre-allocation
- Self-maintaining (compaction)
- SSD-optimized

**Disadvantages**:
- More complex (multiple files)
- Background I/O (compaction)
- Higher memory usage
- Less mature in Nano ecosystem

---

## Migration

### LMDB to RocksDB Migration

**Command**:
```bash
nano_node --migrate_database_lmdb_to_rocksdb
```

**Implementation**: `nano/secure/ledger.cpp` - `migrate_lmdb_to_rocksdb()`

### Migration Process

The migration happens in 7 steps with progress logging:

1. **Blocks Table** (~millions of entries)
   - Progress logged every 500,000 entries
   - Parallel iteration for performance
   - Serializes block + sideband to raw bytes
   - Largest table, takes most time

2. **Pending Table** (~thousands to millions)
   - Progress logged every 100,000 entries
   - Copies all receivable transactions

3. **Confirmation Height Table**
   - Progress logged every 100,000 entries
   - Copies all confirmation data

4. **Accounts Table** (~millions of entries)
   - Progress logged every 100,000 entries
   - Copies all account information

5. **Pruned Table**
   - Progress logged every 100,000 entries
   - Copies pruned block hashes

6. **Peers Table**
   - Progress logged every 5,000,000 entries
   - Copies peer information

7. **Final Votes & Metadata**
   - Copies remaining tables
   - Updates version information

### Migration Features

**Transaction Refresh**:
```cpp
// Prevent excessive memory usage
if (++count % 500000 == 0) {
    lmdb_transaction.refresh ();
    rocksdb_transaction.commit ();
    rocksdb_transaction = store.tx_begin_write ();
}
```

**Disk Space Check**:
- RocksDB requires approximately 65% of LMDB size
- Migration checks available space before starting

**Parallel Processing**:
- Uses parallel iteration where possible
- Multiple threads for large tables

**Safety**:
- Original LMDB database left untouched
- Creates new RocksDB directory
- Can revert to LMDB if needed

### Post-Migration

After successful migration:

1. Update configuration:
```toml
[node]
database_backend = "rocksdb"
```

2. Restart node with new backend

3. Optionally delete old LMDB file:
```bash
rm data.ldb data.ldb-lock
```

---

## Database Upgrades

### Version Management

**Current Version**: 24
**Minimum Version**: 21

**Version Check**: On every node startup

### Upgrade Process

1. **Check Version**: Read from meta table
2. **Create Backup**: If configured in TOML
3. **Sequential Upgrades**: Apply each version upgrade in order
4. **Update Metadata**: Write new version
5. **Optional Vacuum**: LMDB only, if configured

### Version History

| Version | Changes | Notes |
|---------|---------|-------|
| 21 | Minimum supported | Baseline version |
| 22 | Removed unchecked table | Blocks processed immediately |
| 23 | Added rep_weights table | O(1) representative weight lookup |
| 24 | Removed frontiers table | Redundant with accounts.head |

### Upgrade Example (v23 → v24)

**File**: `nano/store/lmdb/lmdb.cpp` or `nano/store/rocksdb/rocksdb.cpp`

```cpp
void upgrade_v23_to_v24 (write_transaction const & tx) {
    // Drop frontiers table (no longer needed)
    drop (tx, tables::frontiers);

    // Update version
    version.put (tx, 24);
}
```

### Backup Before Upgrade

**TOML Configuration**:
```toml
[node]
backup_before_upgrade = true  # Create backup before database upgrades
```

**LMDB Backup**:
```cpp
// Copies entire data.ldb file
std::filesystem::copy (data_path, backup_path);
```

**RocksDB Backup**:
```cpp
// Uses RocksDB backup engine
backup_engine->CreateNewBackup (db);
```

---

## Store Abstraction Layer

### Component Interface

**File**: `nano/store/component.hpp`

The base class for both LMDB and RocksDB implementations:

```cpp
class component {
public:
    // Table references
    store::block & block;
    store::account & account;
    store::pending & pending;
    store::rep_weight & rep_weight;
    store::online_weight & online_weight;
    store::pruned & pruned;
    store::peer & peer;
    store::confirmation_height & confirmation_height;
    store::final_vote & final_vote;
    store::version & version;

    // Transaction methods
    virtual write_transaction tx_begin_write () = 0;
    virtual read_transaction tx_begin_read () const = 0;

    // Operations
    virtual uint64_t count (transaction const &, tables) const = 0;
    virtual int drop (write_transaction const &, tables) = 0;
    virtual bool copy_db (std::filesystem::path const &) = 0;
    virtual void rebuild_db (write_transaction const &) = 0;

    // Version constants
    static int constexpr version_minimum = 21;
    static int constexpr version_current = 24;
};
```

### Table-Specific Interfaces

Each table has its own abstract interface:

**Example: Account Interface** (`nano/store/account.hpp`):
```cpp
class account {
public:
    virtual void put (write_transaction const &, nano::account const &,
                     nano::account_info const &) = 0;
    virtual bool get (transaction const &, nano::account const &,
                     nano::account_info &) const = 0;
    virtual void del (write_transaction const &, nano::account const &) = 0;
    virtual bool exists (transaction const &, nano::account const &) const = 0;

    virtual iterator begin (transaction const &) const = 0;
    virtual iterator end () const = 0;

    virtual void for_each_par (std::function<void(read_transaction const &,
                               iterator, iterator)> const & action) const = 0;
};
```

### Type-Safe Iterators

**Implementation**: `nano/store/iterator.hpp`

```cpp
template<typename Key, typename Value>
class typed_iterator {
    Key key () const;
    Value value () const;

    typed_iterator & operator++ ();  // Pre-increment
    bool operator== (typed_iterator const &) const;
    bool operator!= (typed_iterator const &) const;
};

// Usage:
for (auto it = store.account.begin (tx); it != store.account.end (); ++it) {
    nano::account account = it->first;
    nano::account_info info = it->second;
    // Process account
}
```

### Backend Selection

**File**: `nano/node/make_store.cpp`

```cpp
std::unique_ptr<nano::store::component> make_store (
    nano::logger & logger,
    std::filesystem::path const & path,
    nano::ledger_constants const & constants,
    nano::lmdb_config const & lmdb_config,
    nano::rocksdb_config const & rocksdb_config,
    bool open_read_only)
{
    if (rocksdb_config.enable) {
        return std::make_unique<nano::store::rocksdb::component> (
            logger, path, constants, rocksdb_config, open_read_only);
    } else {
        return std::make_unique<nano::store::lmdb::component> (
            logger, path, constants, lmdb_config, open_read_only);
    }
}
```

---

## Performance Comparison

### Storage Size

For a typical full node:

| Backend | Size | Relative |
|---------|------|----------|
| LMDB | 100% | Baseline |
| RocksDB | ~65% | 35% savings |

### Read Performance

| Operation | LMDB | RocksDB |
|-----------|------|---------|
| Random read | Excellent (zero-copy) | Good (block cache) |
| Sequential scan | Excellent | Good |
| Range query | Excellent | Good |

### Write Performance

| Operation | LMDB | RocksDB |
|-----------|------|---------|
| Random write | Good | Excellent (append-only) |
| Bulk write | Good | Excellent |
| Delete | Moderate | Excellent (tombstone) |

### Memory Usage

| Component | LMDB | RocksDB |
|-----------|------|---------|
| Memory map | Full database | None |
| Block cache | None | Configurable (32MB default) |
| Write buffer | Transaction only | Per-CF (64MB × 11 default) |
| **Total** | Low (few MB) | Medium (~1GB) |

### Disk I/O

| Operation | LMDB | RocksDB |
|-----------|------|---------|
| Read I/O | Page faults (mmap) | Block cache + SSTables |
| Write I/O | B+ tree page updates | WAL + memtable flush |
| Background I/O | None | Compaction (continuous) |

---

## Best Practices

### Choosing a Backend

**Use LMDB when**:
- RAM is abundant (map size = database size)
- Simple deployment preferred (single file)
- Mature, proven solution required
- Read-heavy workload

**Use RocksDB when**:
- Disk space is limited
- Write-heavy workload
- SSD storage available
- Comfortable with more complexity

### Transaction Management

**DO**:
- Use read transactions for queries
- Keep write transactions short
- Commit explicitly when possible
- Use `refresh_if_needed()` for long reads

**DON'T**:
- Hold write transactions across network I/O
- Nest transactions
- Keep transactions open longer than necessary
- Perform expensive computations within transactions

### Iterator Usage

**DO**:
- Use iterators for range queries
- Close iterators when done (automatic with RAII)
- Use parallel iteration for bulk operations

**DON'T**:
- Modify tables while iterating
- Hold iterators across transactions
- Assume iterators are stable after writes

### Maintenance

**LMDB**:
- Run vacuum periodically (monthly or after large changes)
- Monitor map size vs. actual size
- Check for fragmentation

**RocksDB**:
- Monitor compaction I/O
- Adjust thread count for workload
- Check disk space for compaction overhead

---

## Troubleshooting

### LMDB Issues

**"MDB_MAP_FULL" Error**:
- Database has reached map_size limit
- Solution: Increase map_size in configuration
- May require vacuum to reclaim space

**"MDB_TXN_FULL" Error**:
- Too many dirty pages in transaction
- Solution: Split large operations into smaller transactions
- Use `refresh()` during bulk operations

**Slow Performance**:
- Check for fragmentation (vacuum needed)
- Verify map_size is appropriate
- Ensure sync strategy matches requirements

### RocksDB Issues

**High Disk I/O**:
- Compaction running aggressively
- Solution: Adjust io_threads or write_buffer_size
- Consider increasing read_cache

**Out of Disk Space**:
- Compaction requires temporary space
- Solution: Ensure 2x database size available
- Reduce write_cache if memory-constrained

**Slow Startup**:
- WAL replay after crash
- Solution: Normal, wait for replay to complete
- Prevent by clean shutdown

### General Issues

**Database Corruption**:
- Power loss during write (LMDB nosync modes)
- Solution: Restore from backup
- Use safer sync strategy

**Version Mismatch**:
- Database too new for node software
- Solution: Upgrade node software
- Never downgrade database version

**Migration Failure**:
- Insufficient disk space
- Solution: Free space (need ~65% of LMDB size)
- Check logs for specific error

---

## Summary

The Nano node ledger storage is a sophisticated dual-backend system that provides:

- **Flexibility**: Choice between LMDB and RocksDB
- **Performance**: Optimized for both read and write workloads
- **Reliability**: ACID transactions and durability guarantees
- **Efficiency**: Compact binary serialization and indexing
- **Maintainability**: Clean abstraction layer and type safety
- **Scalability**: Support for billions of blocks

The storage layer is the foundation of the Nano node, ensuring data integrity while providing the performance needed for a high-throughput cryptocurrency network.
