# Nano Node Developer Guide

## Introduction

Welcome to the Nano node codebase! This document provides a comprehensive overview of the node architecture, code organization, and key subsystems to help new developers understand how everything fits together.

### What is Nano?

Nano is a digital payment protocol designed for fast, feeless, and eco-friendly transactions. The node software implements the Nano protocol, handling:

- Block creation and validation
- Peer-to-peer networking
- Consensus through vote-weighted Open Representative Voting (ORV)
- Ledger storage and management
- Wallet operations

### Key Characteristics

- **Feeless**: No transaction fees
- **Fast**: Sub-second confirmation times
- **Scalable**: Thousands of transactions per second
- **Green**: Minimal energy consumption (no mining)
- **Decentralized**: Anyone can run a node

---

## Architecture Overview

The Nano node is organized into distinct layers and subsystems:

```
┌─────────────────────────────────────────────────────────────────┐
│                        Application Layer                         │
│                    (RPC, Wallet, WebSocket)                      │
└────────────────────────────┬────────────────────────────────────┘
                             │
┌────────────────────────────┴────────────────────────────────────┐
│                       Consensus Layer                            │
│     (Active Elections, Schedulers, Vote Processing)              │
└────────────────────────────┬────────────────────────────────────┘
                             │
┌────────────────────────────┴────────────────────────────────────┐
│                      Processing Layer                            │
│           (Block Processor, Message Processor)                   │
└────────────────────────────┬────────────────────────────────────┘
                             │
┌────────────────────────────┴────────────────────────────────────┐
│                        Network Layer                             │
│         (P2P Networking, Message Flooding, Channels)             │
└────────────────────────────┬────────────────────────────────────┘
                             │
┌────────────────────────────┴────────────────────────────────────┐
│                         Ledger Layer                             │
│               (Ledger, Store, Transactions)                      │
└─────────────────────────────────────────────────────────────────┘
```

### High-Level Data Flow

```
Network Message Arrives
        ↓
Message Processor (dispatch by type)
        ↓
    ┌───┴────┐
    │        │
Publish    Confirm_Ack
  (block)    (vote)
    │        │
    ↓        ↓
Block     Vote
Processor  Processor
    │        │
    ↓        ↓
Ledger    Active
Insert    Elections
    │        │
    ↓        ↓
Confirmed ← Quorum Reached
    ↓
Cementing
    ↓
Confirmation Height Updated
```

---

## Directory Structure

### Top-Level Organization

```
nano-node/
├── nano/              # Core node implementation
│   ├── lib/           # Shared utilities and primitives
│   ├── secure/        # Ledger and block processing logic
│   ├── store/         # Storage layer abstraction
│   ├── node/          # Main node implementation
│   ├── nano_node/     # Entry point and CLI
│   └── nano_wallet/   # Wallet daemon
├── crypto/            # Cryptographic libraries
├── submodules/        # Third-party dependencies
├── api/               # API definitions
├── systest/           # System tests
└── ci/                # CI/CD scripts
```

### Key Directories Explained

#### `nano/lib/` - Core Utilities

Shared utilities used throughout the codebase:

- **blocks.hpp/cpp**: Block type definitions and implementations
- **numbers.hpp**: Fixed-size integer types (uint128_t, uint256_union, etc.)
- **work.hpp/cpp**: Proof-of-work generation and validation
- **stats.hpp/cpp**: Statistics collection and reporting
- **logging.hpp/cpp**: Logging infrastructure
- **config.hpp/cpp**: Configuration management
- **threading.hpp/cpp**: Threading utilities and thread pools

**Purpose**: Reusable components that don't depend on higher-level node logic.

---

#### `nano/secure/` - Ledger Logic

Core ledger operations and block validation:

- **ledger.hpp/cpp**: Main ledger interface
- **common.hpp**: Common types (account_info, pending_info, vote)
- **ledger_processor.hpp/cpp**: Block validation and processing
- **vote.hpp/cpp**: Vote structure and validation
- **transaction.hpp/cpp**: Database transaction wrappers

**Purpose**: Defines the rules for block validation and ledger state management.

---

#### `nano/store/` - Storage Abstraction

Database storage layer with support for multiple backends:

- **component.hpp/cpp**: Abstract storage interface
- **lmdb/**: LMDB backend implementation
- **rocksdb/**: RocksDB backend implementation
- **tables.hpp**: Database table definitions
- **transaction.hpp**: Transaction abstractions

**Tables**:
- `accounts`: Account information
- `blocks`: All blocks with sidebands
- `pending`: Receivable transactions
- `confirmation_height`: Confirmation tracking
- `rep_weights`: Representative voting weights
- `online_weight`: Historical weight samples
- `peers`: Known network peers

**Purpose**: Provides database independence, allowing swapping between LMDB and RocksDB.

---

#### `nano/node/` - Node Implementation

Main node components and subsystems:

**Core Node**:
- **node.hpp/cpp**: Main node class coordinating all subsystems
- **nodeconfig.hpp/cpp**: Node configuration
- **cli.hpp/cpp**: Command-line interface

**Block Processing**:
- **block_processor.hpp/cpp**: Asynchronous block validation and insertion
- **unchecked_map.hpp/cpp**: Temporary storage for out-of-order blocks

**Consensus**:
- **active_elections.hpp/cpp**: Manages ongoing elections
- **election.hpp/cpp**: Individual election logic
- **vote_processor.hpp/cpp**: Processes incoming votes
- **vote_generator.hpp/cpp**: Generates votes for representatives
- **vote_cache.hpp/cpp**: Caches votes for election resolution

**Scheduling**:
- **scheduler/component.hpp/cpp**: Scheduler coordinator
- **scheduler/priority.hpp/cpp**: Priority-based scheduling (bucket system)
- **scheduler/manual.hpp/cpp**: Manually triggered elections
- **scheduler/hinted.hpp/cpp**: Vote-driven elections
- **scheduler/optimistic.hpp/cpp**: Optimistic confirmation scheduling

**Network**:
- **network.hpp/cpp**: Network coordinator
- **messages.hpp/cpp**: Message type definitions
- **message_processor.hpp/cpp**: Message dispatching
- **transport/**: TCP transport layer
  - **tcp_listener.hpp/cpp**: Connection acceptance
  - **tcp_server.hpp/cpp**: Per-connection message handling
  - **tcp_channel.hpp/cpp**: Bidirectional communication channel
  - **tcp_channels.hpp/cpp**: Channel container

**Telemetry & Discovery**:
- **telemetry.hpp/cpp**: Node telemetry exchange
- **repcrawler.hpp/cpp**: Representative discovery
- **rep_tiers.hpp/cpp**: Representative tier classification
- **online_reps.hpp/cpp**: Online representative tracking

**Bootstrap**:
- **bootstrap_server.hpp/cpp**: Serves bootstrap requests
- **bootstrap_service.hpp/cpp**: Bootstrap client logic

**Wallet**:
- **wallet.hpp/cpp**: Wallet implementation
- **wallets_store.hpp/cpp**: Wallet storage

**Other**:
- **bandwidth_limiter.hpp/cpp**: Traffic shaping
- **peer_exclusion.hpp/cpp**: Peer banning
- **websocket.hpp/cpp**: WebSocket server
- **epoch_upgrader.hpp/cpp**: Protocol version upgrades

**Purpose**: Implements all node functionality coordinated by the main `node` class.

---

#### `nano/nano_node/` - Entry Point

- **entry.cpp**: Main function and CLI handling
- **daemon.hpp/cpp**: Daemon mode implementation
- **benchmarks/**: Performance benchmarks

**Purpose**: Executable entry point and command-line tools.

---

## Core Components

### 1. node (`nano::node`)

**File**: `nano/node/node.hpp`, `nano/node/node.cpp`

The central coordinator that owns and initializes all subsystems.

**Key Responsibilities**:
- Initialize all subsystems
- Coordinate startup and shutdown
- Provide high-level operations (process block, start election, etc.)
- Own all major components

**Major Members**:
```cpp
class node {
    // Storage
    nano::store::component & store;
    nano::ledger & ledger;
    nano::wallets & wallets;

    // Network
    nano::network & network;
    nano::message_processor & message_processor;

    // Processing
    nano::block_processor & block_processor;
    nano::vote_processor & vote_processor;

    // Consensus
    nano::active_elections & active;
    nano::scheduler::component & scheduler;
    nano::vote_cache & vote_cache;

    // Representative tracking
    nano::online_reps & online_reps;
    nano::rep_crawler & rep_crawler;

    // Bootstrap
    nano::bootstrap_service & bootstrap;
    nano::bootstrap_server & bootstrap_server;

    // Workers
    nano::thread_pool & workers;
    nano::work_pool & work;
};
```

**Entry Points**:
- `start()`: Start all subsystems
- `stop()`: Graceful shutdown
- `process()` / `process_local()`: Process blocks
- `start_election()`: Trigger consensus
- `block_confirmed()`: Check confirmation status

---

### 2. ledger (`nano::ledger`)

**File**: `nano/secure/ledger.hpp`, `nano/secure/ledger.cpp`

The authoritative ledger state with all accounts and blocks.

**Key Responsibilities**:
- Validate and insert blocks
- Maintain account balances
- Track representative weights
- Manage confirmation state
- Provide ledger queries

**Key Operations**:
```cpp
// Block processing
block_status process(write_transaction const &, block);

// Rollback
bool rollback(write_transaction const &, block_hash const &);

// Confirmation
std::deque<block> confirm(write_transaction &, block_hash const &);

// Queries
uint128_t weight(account const &) const;
account_info account_get(transaction const &, account const &);
block_hash latest(transaction const &, account const &);
```

**Ledger Cache**:
```cpp
class ledger_cache {
    std::atomic<uint64_t> cemented_count;
    std::atomic<uint64_t> block_count;
    std::atomic<uint64_t> account_count;
};
```

---

### 3. block_processor (`nano::block_processor`)

**File**: `nano/node/block_processor.hpp`, `nano/node/block_processor.cpp`

Asynchronous block validation and ledger insertion.

**Key Responsibilities**:
- Queue blocks from multiple sources
- Validate block signatures and work
- Insert blocks into ledger
- Handle out-of-order blocks (unchecked map)
- Trigger elections for new blocks

**Block Sources**:
```cpp
enum class block_source {
    live,        // From network peers (lowest priority)
    bootstrap,   // From bootstrap process
    local,       // From local wallet/RPC (high priority)
    system,      // From system components (highest priority)
};
```

**Processing Flow**:
```
add(block, source) → Queue
        ↓
Fair queue (round-robin by source)
        ↓
Batch processing (256 blocks)
        ↓
Validate: signature, work, dependencies
        ↓
    Valid?
    ↙    ↘
  Yes     No → Handle error
   ↓
ledger.process() → Insert
   ↓
Trigger election (if not confirmed)
```

**Configuration**:
```cpp
struct block_processor_config {
    size_t batch_size = 256;
    size_t max_peer_queue = 128;
    size_t max_system_queue = 16384;
    size_t priority_live = 1;
    size_t priority_bootstrap = 8;
    size_t priority_local = 16;
    size_t priority_system = 32;
};
```

---

### 4. active_elections (`nano::active_elections`)

**File**: `nano/node/active_elections.hpp`, `nano/node/active_elections.cpp`

Container for all ongoing consensus elections.

**Key Responsibilities**:
- Manage active elections (create, update, remove)
- Route votes to elections
- Detect quorum and trigger confirmation
- Track election timeouts and conflicts
- Limit concurrent elections (capacity management)

**Election Behaviors**:
```cpp
enum class election_behavior {
    priority,      // Bucket-based scheduling
    manual,        // Manually triggered
    hinted,        // Vote-driven
    optimistic,    // Optimistic confirmation
};
```

**Key Operations**:
```cpp
// Start election
insert_result insert(block, behavior, bucket, priority);

// Route vote
void vote(shared_ptr<vote> const &);

// Check status
bool active(qualified_root const &) const;
shared_ptr<election> election(qualified_root const &);

// Cleanup
bool erase(qualified_root const &);
```

**Capacity Management**:
```cpp
struct active_elections_config {
    size_t size = 5000;  // Max concurrent elections
    size_t hinted_limit_percentage = 20;
    size_t optimistic_limit_percentage = 10;
};
```

**Multi-Index Container**:

Elections indexed by:
- `qualified_root`: Primary lookup
- `bucket`: For bucket-based scheduling
- `behavior`: For per-behavior limits
- `difficulty`: For prioritization

---

### 5. scheduler::component (`nano::scheduler::component`)

**File**: `nano/node/scheduler/component.hpp`, `nano/node/scheduler/component.cpp`

Coordinates multiple election schedulers.

**Scheduler Types**:

1. **Priority Scheduler** (`scheduler::priority`)
   - **File**: `nano/node/scheduler/priority.hpp`
   - **Purpose**: Balance-based prioritization using bucket system
   - **How**: 63 buckets by account balance, round-robin activation
   - **Use**: Main scheduling mechanism for unconfirmed blocks

2. **Manual Scheduler** (`scheduler::manual`)
   - **File**: `nano/node/scheduler/manual.hpp`
   - **Purpose**: Explicitly requested elections
   - **How**: Direct election triggering
   - **Use**: RPC-triggered elections, debugging

3. **Hinted Scheduler** (`scheduler::hinted`)
   - **File**: `nano/node/scheduler/hinted.hpp`
   - **Purpose**: Vote-driven election activation
   - **How**: Start elections when votes arrive for unconfirmed blocks
   - **Use**: Respond to representative activity

4. **Optimistic Scheduler** (`scheduler::optimistic`)
   - **File**: `nano/node/scheduler/optimistic.hpp`
   - **Purpose**: Fast-path confirmation for blocks with dependencies confirmed
   - **How**: Activate elections for blocks whose dependencies are confirmed
   - **Use**: Speed up dependent block confirmations

**Coordination**:
```cpp
class scheduler::component {
    priority_scheduler priority;
    manual_scheduler manual;
    hinted_scheduler hinted;
    optimistic_scheduler optimistic;

    // Called periodically
    void activate_priority();
    void activate_hinted();
    void activate_optimistic();
};
```

---

### 6. network (`nano::network`)

**File**: `nano/node/network.hpp`, `nano/node/network.cpp`

Network coordinator managing P2P communication.

**Key Responsibilities**:
- Peer discovery and connection management
- Message flooding (blocks, votes, keepalives)
- Capacity checks
- Network-wide operations

**Key Threads**:
- **cleanup_thread**: Remove stale connections
- **keepalive_thread**: Send periodic keepalives
- **reachout_thread**: Attempt new connections
- **reachout_cached_thread**: Connect to cached peers

**Message Flooding**:
```cpp
// Flood block to PRs + random non-PRs
size_t flood_block_initial(block);

// Flood block to random peers
size_t flood_block(block, traffic_type);

// Flood vote (all peers if PR, subset if non-PR)
size_t flood_vote_pr(vote);
size_t flood_vote_non_pr(vote, scale);

// Flood keepalive
size_t flood_keepalive(scale);
```

**See**: [NETWORK.md](NETWORK.md) for detailed network documentation.

---

### 7. vote_processor (`nano::vote_processor`)

**File**: `nano/node/vote_processor.hpp`, `nano/node/vote_processor.cpp`

Processes incoming votes and routes them to elections.

**Key Responsibilities**:
- Validate vote signatures
- Check vote timestamps
- Route votes to active elections
- Trigger hinted elections
- Track vote statistics

**Processing Flow**:
```
Vote arrives
    ↓
Validate signature
    ↓
Check timestamp (must be increasing)
    ↓
Find active election
    ↓
Election exists?
    ↙         ↘
  Yes          No → Trigger hinted election
   ↓
Route to election
   ↓
Election processes vote
   ↓
Quorum reached? → Confirm block
```

---

### 8. message_processor (`nano::message_processor`)

**File**: `nano/node/message_processor.hpp`, `nano/node/message_processor.cpp`

Asynchronous message dispatching with fair queuing.

**Key Responsibilities**:
- Receive messages from network
- Fair queue by channel (prevent single peer monopolizing)
- Dispatch to handlers via visitor pattern

**Message Types**:
- `keepalive`: Peer discovery
- `publish`: Block broadcast
- `confirm_req`: Vote solicitation
- `confirm_ack`: Vote
- `telemetry_req`/`telemetry_ack`: Node info exchange
- `node_id_handshake`: Identity verification
- `asc_pull_req`/`asc_pull_ack`: Bootstrap protocol

**Fair Queue**:

Prevents single channel from monopolizing processor:
```cpp
template<typename T, typename Tag>
class fair_queue {
    std::unordered_map<Tag, std::deque<T>> queues;
    std::deque<Tag> order;  // Round-robin

    void push(T value, Tag tag);
    T pop();  // Round-robin across channels
};
```

---

### 9. store::component (`nano::store::component`)

**File**: `nano/store/component.hpp`, `nano/store/lmdb/lmdb.cpp`, `nano/store/rocksdb/rocksdb.cpp`

Abstract storage interface supporting multiple backends.

**Backends**:
- **LMDB**: Memory-mapped B+ tree (single file: `data.ldb`)
- **RocksDB**: LSM-tree (directory: `rocksdb/`)

**Tables**:
```cpp
enum class tables {
    accounts,            // Account information
    blocks,              // Blocks with sidebands
    pending,             // Receivable transactions
    confirmation_height, // Confirmation tracking
    rep_weights,         // Representative weights
    online_weight,       // Historical weight samples
    pruned,              // Pruned block hashes
    peers,               // Known peers
    final_votes,         // Final vote cache
    meta,                // Database metadata
};
```

**Operations**:
```cpp
// Transactions
write_transaction tx_begin_write();
read_transaction tx_begin_read();

// Table access
store.account.put(tx, account, account_info);
store.account.get(tx, account);
store.block.put(tx, hash, block, sideband);
store.block.get(tx, hash);
```

**See**: [LEDGER_STORAGE.md](LEDGER_STORAGE.md) for detailed storage documentation.

---

## Data Flow Examples

### Example 1: Block Propagation

```
1. Node A creates/receives block
        ↓
2. Block Processor validates and inserts
        ↓
3. Ledger inserts block (unconfirmed)
        ↓
4. network.flood_block_initial()
        ↓
5. Sent to PRs + random non-PRs
        ↓
6. Remote nodes receive via tcp_server
        ↓
7. Message Processor dispatches
        ↓
8. Block Processor queues
        ↓
9. Repeat steps 2-4 (rebroadcast)
```

### Example 2: Election and Confirmation

```
1. Scheduler activates unconfirmed block
        ↓
2. active_elections.insert(block)
        ↓
3. election.broadcast_vote_request()
        ↓
4. network.flood_confirm_req()
        ↓
5. Representatives receive confirm_req
        ↓
6. vote_generator.generate()
        ↓
7. network.flood_vote()
        ↓
8. Nodes receive confirm_ack (vote)
        ↓
9. vote_processor routes to election
        ↓
10. election.vote()
        ↓
11. Accumulate voting weight
        ↓
12. Quorum reached (>50% online weight)?
        ↓
13. election.confirm()
        ↓
14. ledger.confirm() marks block confirmed
        ↓
15. Trigger cementing (confirmation height)
```

### Example 3: Receive Transaction

```
1. User has pending transaction (send from another account)
        ↓
2. Wallet queries ledger for receivables
        ↓
3. ledger.any.pending_get(account)
        ↓
4. Wallet creates receive block
        ↓
5. Work generation (PoW)
        ↓
6. node.process_local(block)
        ↓
7. Block Processor (local source = high priority)
        ↓
8. Ledger validation and insertion
        ↓
9. Remove from pending table
        ↓
10. Update account balance
        ↓
11. network.flood_block_initial()
        ↓
12. Priority Scheduler queues for election
        ↓
13. Election started
        ↓
14. (See Example 2 for election flow)
```

---

## Key Subsystems

### Consensus System

**Components**:
- Active Elections Container
- Vote Processor
- Vote Cache
- Schedulers
- Confirmation Height Processor

**How Consensus Works**:

1. **Election Activation**: Schedulers determine which unconfirmed blocks need elections
2. **Vote Solicitation**: Nodes broadcast `confirm_req` to representatives
3. **Vote Generation**: Representatives validate and sign votes
4. **Vote Propagation**: Votes broadcast as `confirm_ack` messages
5. **Vote Processing**: Votes routed to elections, weight accumulated
6. **Quorum Detection**: When >50% online weight votes for same hash
7. **Confirmation**: Block marked confirmed, election stops
8. **Cementing**: Confirmation height updated, block permanently confirmed

**Vote Weight Calculation**:
- Each account delegates its balance to a representative
- Representative's weight = sum of delegated balances
- Online weight = sum of weights of recently active representatives
- Quorum threshold = 50% of online weight

**See**: [BUCKET_SYSTEM.md](BUCKET_SYSTEM.md) for priority scheduling details.

---

### Bootstrap System

**Purpose**: Sync ledger from peers when starting or catching up.

**Components**:
- `bootstrap_service`: Client-side bootstrap coordination
- `bootstrap_server`: Server-side bootstrap request handling
- `bootstrap_weights`: Preconfigured representative weights for initial sync

**Bootstrap Types**:

1. **Legacy Bootstrap**:
   - `bulk_pull`: Request blocks by account
   - `bulk_push`: Send blocks
   - `frontier_req`: Request account frontiers

2. **Ascending Bootstrap** (preferred):
   - `asc_pull_req` / `asc_pull_ack`: Request/response protocol
   - More efficient, request by hash or account
   - Better for targeted sync

**Bootstrap Process**:
```
1. Node starts, detects incomplete ledger
2. Connect to bootstrap peers
3. Request account frontiers
4. Request blocks for accounts
5. Process blocks via Block Processor
6. Continue until caught up
7. Transition to normal operation
```

---

### Representative System

**Purpose**: Track representatives and their voting weight.

**Components**:
- `rep_crawler`: Discovers representatives via network
- `rep_tiers`: Classifies representatives by weight
- `online_reps`: Tracks which representatives are currently online

**Representative Tiers**:
```cpp
enum class rep_tier {
    none,                // < 0.001% of online weight
    tier_1,              // 0.001% - 0.1%
    tier_2,              // 0.1% - 1%
    tier_3,              // 1% - 5%
    principal_representative  // ≥ 0.1% (special treatment)
};
```

**Principal Representatives (PRs)**:
- Have ≥ 0.1% of online weight
- Receive priority for blocks and confirmation requests
- Votes broadcast to all peers (not just subset)
- Critical for fast consensus

**Online Weight Tracking**:
- Representatives marked online when they vote
- Online weight = sum of weights of online representatives
- Quorum threshold = 50% of online weight
- Updated continuously as representatives vote

---

### Work System

**Purpose**: Proof-of-work generation for spam prevention.

**Components**:
- `work_pool`: Local work generation
- `distributed_work_factory`: Coordinate work generation across multiple sources

**Work Generation**:
- Uses Blake2b-based PoW
- Difficulty varies by block type (send vs. receive)
- Can use OpenCL for GPU acceleration
- Can distribute to work peers

**Work Sources**:
```cpp
enum class work_source {
    local,          // Local CPU/GPU
    peers,          // Network work peers
    cached,         // Previously computed work
};
```

**Usage**:
```cpp
// Generate work for block
node.work_generate(work_version, root, difficulty,
    [](std::optional<uint64_t> work) {
        if (work) {
            // Use work for block
            block.work_set(*work);
        }
    });
```

---

### Wallet System

**Components**:
- `wallet`: Individual wallet logic
- `wallets`: Container for multiple wallets
- `wallets_store`: Wallet persistence

**Wallet Operations**:
- Create and manage accounts
- Sign blocks
- Search for receivables
- Automatic receive
- Change representative

**Wallet Storage**:
- Encrypted with password
- Stored in `wallets.ldb` (LMDB)
- Contains private keys for accounts

**Security Note**: Wallets are encrypted but stored on disk. Production systems should use secure key management.

---

## Getting Started for Developers

### Building the Node

#### Prerequisites

- **C++20 compiler**: GCC 10+, Clang 12+, MSVC 2019+
- **CMake**: 3.20+
- **Boost**: 1.75+
- **Qt**: 5.15+ (for GUI wallet, optional)

#### Build Steps

```bash
# Clone repository
git clone https://github.com/nanocurrency/nano-node.git --recursive
cd nano-node

# Create build directory
mkdir build && cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . -j$(nproc)

# Run tests
ctest -j$(nproc)

# Run node
./nano_node --help
```

#### CMake Options

```bash
# Enable tests
cmake .. -DNANO_TEST=ON

# Enable GUI
cmake .. -DNANO_GUI=ON

# Use RocksDB instead of LMDB
cmake .. -DNANO_ROCKSDB=ON

# Enable OpenCL work generation
cmake .. -DNANO_OPENCL=ON
```

---

### Code Style

The project follows these conventions:

**Naming**:
- **Classes**: `snake_case` (e.g., `block_processor`)
- **Functions**: `snake_case` (e.g., `process_block()`)
- **Variables**: `snake_case` (e.g., `block_hash`)
- **Constants**: `snake_case` (e.g., `max_queue_size`)
- **Enum values**: `snake_case` (e.g., `block_source::live`)

**Formatting**:
- Tabs for indentation
- Braces on new line
- Use `.clang-format` configuration in repository

**Example**:
```cpp
class my_class final
{
public:
	void my_function (int parameter)
	{
		if (condition)
		{
			// Do something
		}
	}

private:
	int member_variable{ 0 };
};
```

---

### Running the Node

#### Basic Usage

```bash
# Start node with default configuration
./nano_node --daemon

# Specify data directory
./nano_node --daemon --data_path /path/to/data

# Use different network (testnet)
./nano_node --daemon --network=test
```

#### Configuration

Configuration in `config-node.toml`:

```toml
[node]
peering_port = 7075
enable_voting = true
database_backend = "lmdb"

[node.network]
max_peers_per_ip = 4

[node.block_processor]
max_peer_queue = 128
batch_size = 256

[node.active_elections]
size = 5000
```

#### RPC Interface

Enable RPC in `config-rpc.toml`:

```toml
[rpc]
enable = true
address = "::1"
port = 7076
```

Example RPC call:
```bash
curl -d '{
  "action": "block_count"
}' http://localhost:7076
```

---

### Debugging Tips

#### Enable Debug Logging

```bash
# In config-node.toml
[node.logging]
min_time_between_log_output = 0
min_time_between_output_level_log_output = 0

[node.logging.log_to_cerr]
enable = true
```

#### GDB

```bash
# Build with debug symbols
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Run under GDB
gdb --args ./nano_node --daemon
```

#### Sanitizers

```bash
# Address Sanitizer
cmake .. -DCMAKE_BUILD_TYPE=Debug -DNANO_ASAN=ON

# Thread Sanitizer
cmake .. -DCMAKE_BUILD_TYPE=Debug -DNANO_TSAN=ON

# Undefined Behavior Sanitizer
cmake .. -DCMAKE_BUILD_TYPE=Debug -DNANO_UBSAN=ON
```

#### Common Debug Points

**Block Processing Issues**:
- Set breakpoint in `block_processor::process_one()`
- Check `ledger.process()` return value
- Examine block validation in `ledger_processor`

**Election Issues**:
- Set breakpoint in `active_elections::insert()`
- Check `election::vote()` for vote processing
- Examine `election::confirm()` for quorum detection

**Network Issues**:
- Set breakpoint in `message_processor::process()`
- Check `tcp_server::receive_message()` for incoming data
- Examine `network::flood_*()` for outgoing messages

---

### Testing

#### Unit Tests

```bash
# Build with tests enabled
cmake .. -DNANO_TEST=ON

# Run all tests
ctest

# Run specific test
./core_test --gtest_filter=block_store.*

# Run with verbose output
./core_test --gtest_filter=block_store.* --gtest_print_time=1
```

#### System Tests

```bash
# System tests in systest/
cd systest
python3 -m pytest tests/
```

#### Benchmarks

```bash
# Run benchmarks
./nano_node --benchmark_block_processing
./nano_node --benchmark_elections
./nano_node --benchmark_cementing
```

**See**: [BENCHMARK.md](BENCHMARK.md) for benchmark documentation.

---

### Contributing

#### Development Workflow

1. **Fork** the repository on GitHub
2. **Clone** your fork locally
3. **Create branch** for your feature: `git checkout -b feature/my-feature`
4. **Make changes** with clear commits
5. **Test** your changes thoroughly
6. **Format** code: `clang-format -i <files>`
7. **Push** to your fork: `git push origin feature/my-feature`
8. **Create Pull Request** on GitHub

#### Pull Request Guidelines

- Clear description of changes
- Reference related issues
- Include tests for new functionality
- Ensure CI passes
- Follow code style
- Update documentation if needed

#### Code Review Process

1. Automated checks (CI, formatting, tests)
2. Peer review by maintainers
3. Address feedback
4. Approval and merge

---

## Important Files Reference

### Entry Points

| File | Purpose |
|------|---------|
| `nano/nano_node/entry.cpp` | Main function, CLI handling |
| `nano/nano_node/daemon.cpp` | Daemon mode implementation |
| `nano/nano_wallet/entry.cpp` | Wallet daemon entry point |

### Core Classes

| File | Class | Purpose |
|------|-------|---------|
| `nano/node/node.hpp` | `nano::node` | Main node coordinator |
| `nano/secure/ledger.hpp` | `nano::ledger` | Ledger state and validation |
| `nano/store/component.hpp` | `nano::store::component` | Storage abstraction |
| `nano/node/block_processor.hpp` | `nano::block_processor` | Block validation queue |
| `nano/node/active_elections.hpp` | `nano::active_elections` | Election container |
| `nano/node/network.hpp` | `nano::network` | Network coordinator |

### Data Structures

| File | Type | Purpose |
|------|------|---------|
| `nano/lib/blocks.hpp` | Blocks | Block type definitions |
| `nano/lib/numbers.hpp` | Numbers | Fixed-size integers |
| `nano/secure/common.hpp` | Common types | account_info, pending_info, vote |
| `nano/lib/config.hpp` | Configuration | Network parameters |

### Protocol

| File | Purpose |
|------|---------|
| `nano/node/messages.hpp` | Network message definitions |
| `nano/secure/vote.hpp` | Vote structure and validation |
| `nano/lib/work.hpp` | Proof-of-work |

### Storage

| File | Purpose |
|------|---------|
| `nano/store/lmdb/lmdb.cpp` | LMDB backend |
| `nano/store/rocksdb/rocksdb.cpp` | RocksDB backend |
| `nano/store/tables.hpp` | Table definitions |

### Utilities

| File | Purpose |
|------|---------|
| `nano/lib/stats.hpp` | Statistics collection |
| `nano/lib/logging.hpp` | Logging infrastructure |
| `nano/lib/threading.hpp` | Thread management |
| `nano/lib/timer.hpp` | Performance timing |

---

## Additional Documentation

For deeper dives into specific subsystems:

- **[BUCKET_SYSTEM.md](BUCKET_SYSTEM.md)**: Priority-based election scheduling
- **[LEDGER_STORAGE.md](LEDGER_STORAGE.md)**: Storage layer and database tables
- **[NETWORK.md](NETWORK.md)**: Network implementation and protocol
- **[BENCHMARK.md](BENCHMARK.md)**: Performance benchmarking

---

## Common Development Tasks

### Adding a New Message Type

1. Add enum value to `nano::message_type` in `messages.hpp`
2. Create message class inheriting from `nano::message`
3. Implement `serialize()`, `deserialize()`, and `visit()`
4. Add handler to `message_visitor`
5. Update `deserialize_message()` to handle new type
6. Add processing logic in message_processor or relevant handler

### Adding a New RPC Command

1. Add action to RPC handler in `nano/rpc/handlers/*.cpp`
2. Implement handler function
3. Add to RPC command documentation
4. Add tests in `nano/rpc_test/*.cpp`

### Modifying Database Schema

1. Update table definition in relevant `nano/store/*.hpp` file
2. Implement upgrade function in `nano/store/lmdb/lmdb.cpp` and `nano/store/rocksdb/rocksdb.cpp`
3. Increment database version in `nano/store/versioning.hpp`
4. Test migration from old version
5. Update documentation in `LEDGER_STORAGE.md`

### Adding Configuration Option

1. Add member to config class (e.g., `nano::node_config`)
2. Implement serialization in `serialize()` and `deserialize()`
3. Add default value
4. Update `config-node.toml.sample`
5. Document in appropriate `.md` file

---

## Community and Support

### Resources

- **Documentation**: https://docs.nano.org
- **Discord**: https://chat.nano.org
- **Reddit**: https://reddit.com/r/nanocurrency
- **GitHub**: https://github.com/nanocurrency/nano-node

### Getting Help

- Check existing documentation
- Search GitHub issues
- Ask on Discord (#development channel)
- Create GitHub issue with:
  - Clear description
  - Steps to reproduce
  - Expected vs. actual behavior
  - Version information
  - Relevant logs

### Reporting Bugs

Include:
- Operating system and version
- Node version (`./nano_node --version`)
- Configuration (sanitized)
- Detailed steps to reproduce
- Log output (sanitized)
- Expected behavior

---

## Summary

The Nano node is a sophisticated piece of software implementing a novel distributed consensus protocol. Key points:

- **Modular Architecture**: Clear separation between layers (network, processing, consensus, storage)
- **High Performance**: Asynchronous I/O, parallel processing, efficient data structures
- **Multiple Subsystems**: Each with specific responsibilities, coordinated by the `node` class
- **Pluggable Storage**: Abstract storage layer supporting LMDB and RocksDB
- **Comprehensive Testing**: Unit tests, integration tests, system tests, and benchmarks

Understanding the data flow (network → processing → consensus → storage) is key to navigating the codebase. Start with high-level components (`node`, `ledger`, `network`) and drill down into specific subsystems as needed.

Welcome to Nano development! 🎉
