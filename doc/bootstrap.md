# Nano Node Bootstrap System

## Overview

The bootstrap system enables Nano nodes to synchronize with the network by downloading missing blocks from peers. Unlike traditional blockchain bootstrapping which downloads a linear chain, Nano's block-lattice architecture requires a more sophisticated approach that can synchronize multiple independent account chains concurrently.

The system employs **four parallel strategies** that work together to ensure fast, reliable, and secure synchronization:

1. **Priority-based Bootstrap** - Targets recently referenced or high-priority accounts
2. **Database Scan** - Systematically checks all local accounts for updates
3. **Dependency Walker** - Resolves missing source blocks that prevent account processing
4. **Frontier Scan** - Discovers outdated accounts by querying network peers

## Architecture

### Client-Server Model

The bootstrap system uses a **client-server architecture**:

#### Bootstrap Service (Client)
**Location**: `nano/node/bootstrap/bootstrap_service.hpp/cpp`

The client component that runs on nodes requesting blocks. It:
- Manages four concurrent bootstrap strategies
- Sends `asc_pull_req` (Ascending Pull Request) messages to peers
- Processes `asc_pull_ack` (Ascending Pull Acknowledgment) responses
- Queues received blocks for processing
- Tracks peer performance and manages channel selection
- Handles account priorities and dependency resolution

**Key threads**:
- `priorities_thread` - Processes high-priority accounts
- `database_thread` - Scans local database for accounts to sync
- `dependencies_thread` - Resolves missing block dependencies
- `frontiers_thread` - Discovers outdated accounts via frontier queries
- `cleanup_thread` - Handles timeouts and maintenance

#### Bootstrap Server (Server)
**Location**: `nano/node/bootstrap/bootstrap_server.hpp/cpp`

The server component that responds to bootstrap requests from other nodes. It:
- Receives `asc_pull_req` messages from peers
- Queries local ledger for requested data
- Sends `asc_pull_ack` responses with blocks, account info, or frontiers
- Rate limits responses to prevent resource exhaustion
- Processes requests in batches for efficiency

### Component Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                        Nano Node                             │
│                                                              │
│  ┌────────────────────────────────────────────────────┐    │
│  │         Bootstrap Service (Client)                  │    │
│  │                                                      │    │
│  │  ┌──────────────┐  ┌──────────────┐               │    │
│  │  │ Priority     │  │ Database     │               │    │
│  │  │ Bootstrap    │  │ Scan         │               │    │
│  │  └──────┬───────┘  └──────┬───────┘               │    │
│  │         │                  │                        │    │
│  │  ┌──────▼──────┐  ┌───────▼───────┐               │    │
│  │  │ Dependency  │  │ Frontier      │               │    │
│  │  │ Walker      │  │ Scan          │               │    │
│  │  └──────┬──────┘  └───────┬───────┘               │    │
│  │         │                  │                        │    │
│  │         └────────┬─────────┘                        │    │
│  │                  │                                   │    │
│  │         ┌────────▼──────────┐                       │    │
│  │         │  Request Manager   │                       │    │
│  │         │  • Peer Scoring    │                       │    │
│  │         │  • Rate Limiting   │                       │    │
│  │         │  • Timeout Mgmt    │                       │    │
│  │         └────────┬───────────┘                       │    │
│  └──────────────────┼───────────────────────────────────┘    │
│                     │                                         │
│                     │ asc_pull_req                           │
│                     ▼                                         │
│           ┌─────────────────────┐                            │
│           │   Network Layer     │                            │
│           └─────────┬───────────┘                            │
│                     │                                         │
│                     │ asc_pull_ack                           │
│                     ▼                                         │
│  ┌──────────────────────────────────────────────────────┐   │
│  │         Bootstrap Server (Server)                     │   │
│  │                                                        │   │
│  │  ┌────────────┐    ┌────────────┐                    │   │
│  │  │  Request   │───▶│   Ledger   │                    │   │
│  │  │  Queue     │    │   Query    │                    │   │
│  │  └────────────┘    └────────────┘                    │   │
│  │                                                        │   │
│  └────────────────────────────────────────────────────────┘  │
│                     │                                         │
│                     ▼                                         │
│           ┌─────────────────────┐                            │
│           │  Block Processor    │                            │
│           └─────────────────────┘                            │
└──────────────────────────────────────────────────────────────┘
```

## Bootstrap Strategies

### 1. Priority-Based Bootstrap

**Status**: Enabled by default (`enable_priorities = true`)

**Purpose**: Rapidly synchronize accounts that are actively being used or referenced.

**How it works**:

1. **Account Prioritization**:
   - Accounts start with priority 2.0
   - Successfully synced accounts: priority × 2 (max 128.0)
   - Empty responses: priority ÷ 2
   - Priority < 0.15: account removed from priority set

2. **Request Strategy**:
   - Pull count scales with priority: `min(max(2, priority), 128)` blocks
   - Higher priority accounts request more blocks per query
   - Accounts have 3-second cooldown between requests

3. **Request Types** (Optimistic vs Safe):
   - **Optimistic** (75% of requests): Start from unconfirmed frontier (fast but vulnerable to forks)
   - **Safe** (25% of requests): Start from confirmed frontier (slower but immune to poisoning)

4. **Block Insertion**:
   - Blocks queued to block_processor with `block_source::bootstrap`
   - Successful insertions trigger priority increase
   - Send blocks trigger destination account prioritization

**Configuration**:
```toml
[bootstrap]
enable_priorities = true
priorities_max = 262144  # Max 256K priority accounts
optimistic_request_percentage = 75
```

**Files**: `bootstrap_service.cpp:667-677`, `account_sets.cpp`

---

### 2. Database Scan

**Status**: Disabled by default (`enable_database_scan = false`)

**Purpose**: Systematically verify all local accounts are up-to-date by iterating the entire database.

**How it works**:

1. **Iteration**:
   - Scans both accounts table and pending table
   - Processes 512 accounts per batch
   - Requests 2 blocks per account
   - Loops continuously (wraps to start after reaching end)

2. **Throttling**:
   - Tracks success rate using sliding window
   - Slows down when no new blocks are received
   - "Warms up" after completing first full iteration
   - Uses dedicated rate limiter (250 req/sec)

3. **Use Cases**:
   - Verifying database integrity
   - Catching missed blocks from all strategies
   - Useful for nodes that have been offline for extended periods

**Configuration**:
```toml
[bootstrap]
enable_database_scan = false  # Usually disabled (resource intensive)
database_rate_limit = 250
database_warmup_ratio = 10
```

**Files**: `bootstrap_service.cpp:695-707`, `database_scan.cpp`

---

### 3. Dependency Walker

**Status**: Enabled by default (`enable_dependency_walker = true`)

**Purpose**: Resolve missing source blocks that prevent account chains from being processed.

**How it works**:

1. **Blocking Detection**:
   - Block processor returns `gap_source` for blocks missing their source
   - Account marked as blocked with the missing block hash
   - Account removed from priority set

2. **Dependency Resolution**:
   - Dependency walker requests account info for missing hash
   - Response reveals which account contains the missing block
   - That account is prioritized for bootstrapping
   - Original account unblocked when dependency is inserted

3. **Automatic Decay**:
   - Blocked entries older than 15 minutes are automatically removed
   - Prevents stale dependencies from accumulating
   - Periodic sync (every 60 seconds) re-adds known dependencies

**Example Flow**:
```
1. Block A arrives, requires source block B (missing)
   → Account X blocked on hash B

2. Dependency walker requests: "Which account contains hash B?"
   → Response: "Account Y"

3. Account Y prioritized for bootstrap
   → Block B received and inserted

4. Account X unblocked
   → Block A can now be processed
```

**Configuration**:
```toml
[bootstrap.account_sets]
blocking_max = 262144       # Max 256K blocked accounts
blocking_decay = "15m"      # 15 minute TTL
```

**Files**: `bootstrap_service.cpp:725-735`, `account_sets.cpp:126-144`

---

### 4. Frontier Scan

**Status**: Enabled by default (`enable_frontier_scan = true`)

**Purpose**: Discover accounts with outdated frontiers by querying random peers across the network.

**How it works**:

1. **Range Division**:
   - Divides account space into 128 parallel ranges
   - Each range handled independently with 5-second cooldown

2. **Peer Querying**:
   - Requests up to 1000 frontiers per range from random peers
   - Queries 4 different peers for each range (redundancy)
   - Processes responses asynchronously in worker threads

3. **Frontier Comparison**:
   - Uses database crawlers to efficiently compare frontiers
   - Detects accounts with mismatched heads
   - Detects accounts with pending blocks but no local state

4. **Prioritization**:
   - Outdated accounts added to priority set with low priority (0.15)
   - Prevents frontier scan from overwhelming priority bootstrap
   - Allows other sources to increase priority if needed

**Algorithm**:
```
For each of 128 ranges:
  1. Select random peer
  2. Request frontiers: account range [start, start + 2^120)
  3. Compare received frontiers with local ledger
  4. For each mismatch:
     - If local < remote: add to priority set (priority 0.15)
     - If local has pending: add to priority set
  5. Wait 5 seconds cooldown
  6. Repeat with different peer
```

**Configuration**:
```toml
[bootstrap.frontier_scan]
head_parallelism = 128         # Parallel ranges
consideration_count = 4        # Peers to query per range
candidates = 1000              # Max frontiers per response
cooldown = "5s"               # Range cooldown
frontier_rate_limit = 8       # Overall rate limit (req/sec)
```

**Files**: `bootstrap_service.cpp:762-772`, `frontier_scan.cpp`

---

## Bootstrap Protocol

### Message Types

#### asc_pull_req (Request)

Request message sent by bootstrap client to peers.

**Structure**:
```cpp
struct asc_pull_req {
    asc_pull_type type;    // blocks, account_info, frontiers
    uint64_t id;           // Random ID for matching responses
    payload_variant payload;
};
```

**Request Types**:

1. **blocks** - Request blocks from an account chain
   ```cpp
   blocks_payload {
       hash_or_account start;    // Starting point
       uint8_t count;            // Number of blocks (1-128)
       start_type type;          // hash or account
   }
   ```

2. **account_info** - Request account metadata
   ```cpp
   account_info_payload {
       hash_or_account target;
       target_type type;
   }
   ```

3. **frontiers** - Request frontier list
   ```cpp
   frontiers_payload {
       account start;       // Starting account
       uint16_t count;      // Number of frontiers (1-1000)
   }
   ```

#### asc_pull_ack (Response)

Response message sent by bootstrap server to requesting peer.

**Structure**:
```cpp
struct asc_pull_ack {
    asc_pull_type type;    // Matches request type
    uint64_t id;           // Matches request ID
    payload_variant payload;
};
```

**Response Payloads**:

1. **blocks** - Chain of blocks
   ```cpp
   blocks_payload {
       std::deque<std::shared_ptr<block>> blocks;  // Max 128
   }
   ```

2. **account_info** - Account metadata
   ```cpp
   account_info_payload {
       account address;
       hash account_open;             // Open block
       hash account_head;             // Current frontier
       uint64_t account_block_count;
       hash account_conf_frontier;    // Confirmed frontier
       uint64_t account_conf_height;
   }
   ```

3. **frontiers** - List of account frontiers
   ```cpp
   frontiers_payload {
       std::deque<std::pair<account, hash>> frontiers;  // Max 1000
   }
   ```

### Request/Response Flow

#### Client Side (bootstrap_service.cpp)

```
1. Wait for available channel
   ├─ Check peer scoring (select least busy peer)
   ├─ Check rate limiter allowance
   └─ Check block processor capacity

2. Build request
   ├─ Generate random request ID
   ├─ Select request type (blocks/account_info/frontiers)
   ├─ Choose optimistic (75%) or safe (25%) start point
   └─ Set pull count based on priority

3. Send request
   ├─ Send via channel with traffic_type::bootstrap_requests
   ├─ Store async_tag with timeout (60 seconds)
   └─ Update peer scoring (increment outstanding count)

4. Process response (when received)
   ├─ Match response ID with pending request
   ├─ Validate response contents
   ├─ Queue blocks to block_processor
   ├─ Update account priority/timestamp
   └─ Update peer scoring (decrement outstanding count)

5. Handle timeout
   ├─ Cleanup thread checks every 5 seconds
   ├─ Remove tags with expired cutoff time
   └─ Peer scoring will auto-decay stale entries
```

#### Server Side (bootstrap_server.cpp)

```
1. Receive request
   ├─ Message arrives via message_processor
   ├─ Verify request validity
   └─ Check channel capacity (max 16 per peer)

2. Queue request
   ├─ Add to fair_queue (prioritizes by peer)
   └─ Wake processing thread

3. Process batch
   ├─ Dequeue up to 64 requests
   ├─ Apply rate limiting (500 req/sec)
   └─ Process each request:
       ├─ Begin read transaction
       ├─ Query ledger for requested data
       └─ Build response payload

4. Send response
   ├─ Construct asc_pull_ack with matching ID
   ├─ Send via channel with traffic_type::bootstrap_server
   └─ Transaction automatically committed/rolled back
```

### Protocol Validation

**Request Validation** (server side):
- Verify request ID is non-zero
- Check pull count within limits (1-128 for blocks, 1-1000 for frontiers)
- Validate account/hash format

**Response Validation** (client side):
- Match response ID with pending request
- Verify response type matches request type
- For block chains:
  - First block matches requested start point
  - Blocks form valid chain (each block.previous() = previous block.hash())
  - Block count doesn't exceed requested amount
- For frontiers:
  - Frontiers in ascending account order
  - No duplicate accounts

**Security Considerations**:
- Invalid responses logged but currently don't penalize peers (TODO)
- Optimistic requests vulnerable to bootstrap poisoning (mitigated by 25% safe requests)
- Rate limiting prevents DoS attacks
- Channel limits prevent resource exhaustion

---

## Performance Characteristics

### Bandwidth Management

**Rate Limiting**:
```cpp
limiter            500 req/sec   // Overall request rate
database_limiter   250 req/sec   // Database scan only
frontiers_limiter    8 req/sec   // Frontier scan only
```

**Channel Limits**:
- Max 16 outstanding requests per peer
- Max 1024 total in-flight requests
- Automatic backoff when limits reached

**Block Throughput**:
- Up to 128 blocks per request
- 256 blocks processed per batch by block_processor
- Effective throughput: ~10K-50K blocks/sec depending on network

### Concurrency

**Client Threads**:
- 4 strategy threads (priorities, database, dependencies, frontiers)
- 1 cleanup thread
- Asynchronous I/O via Boost.Asio

**Server Threads**:
- Configurable (default: 1)
- Processes 64 requests per batch
- Rate limited to 500 req/sec

### Memory Usage

**Account Sets**:
- Priority set: max 256K accounts × ~64 bytes = ~16 MB
- Blocking set: max 256K accounts × ~48 bytes = ~12 MB

**In-flight Requests**:
- Max 1024 requests × ~200 bytes = ~200 KB

**Block Queue**:
- Block processor handles queuing
- Bootstrap respects block_processor_threshold (1000 blocks)

### Synchronization Time

**Factors**:
- Ledger size (blocks to sync)
- Network conditions (peer availability, bandwidth)
- Node resources (CPU, disk I/O)
- Database backend (RocksDB vs LMDB)

**Typical Performance**:
- Fresh node (empty ledger): 2-8 hours for full sync (100M+ blocks)
- Partially synced node: Minutes to hours depending on gap
- Live node (minor gaps): Seconds to minutes

**Optimization Tips**:
- Use RocksDB for faster write performance
- Increase `block_processor.batch_size` to 512 on fast systems
- Enable priority bootstrap only (disable database scan)
- Ensure adequate network peers (`peers_per_ip = 1`, good peer connectivity)

---

## Configuration Reference

### bootstrap_config

**File**: `nano/node/bootstrap/bootstrap_config.hpp`

```toml
[bootstrap]
# Master enable/disable
enable = true

# Strategy toggles
enable_priorities = true
enable_database_scan = false         # Usually disabled (intensive)
enable_dependency_walker = true
enable_frontier_scan = true

# Request limits
channel_limit = 16                   # Max requests per peer
max_requests = 1024                  # Total in-flight requests
max_pull_count = 128                 # Max blocks per request

# Rate limits (requests per second)
rate_limit = 500                     # Overall rate
database_rate_limit = 250            # Database scan rate
frontier_rate_limit = 8              # Frontier scan rate

# Thresholds
block_processor_threshold = 1000     # Block processor capacity check
database_warmup_ratio = 10           # Weight factor for DB scan

# Behavior
optimistic_request_percentage = 75   # % of optimistic requests (vs safe)

# Timeouts
request_timeout = "15s"              # Request timeout
throttle_wait = "100ms"              # Max backoff delay
```

### account_sets_config

```toml
[bootstrap.account_sets]
consideration_count = 4              # Priority sampling iterations
priorities_max = 262144              # Max priority accounts (256K)
blocking_max = 262144                # Max blocked accounts (256K)
cooldown = "3s"                      # Account request cooldown
blocking_decay = "15m"               # Blocked entry TTL
```

### frontier_scan_config

```toml
[bootstrap.frontier_scan]
head_parallelism = 128               # Parallel ranges
consideration_count = 4              # Peers sampled per range
candidates = 1000                    # Max frontiers per response
cooldown = "5s"                      # Range cooldown
max_pending = 16                     # Worker queue limit
```

### bootstrap_server_config

```toml
[bootstrap_server]
enable = true                        # Server enable/disable
max_queue = 16                       # Max queue size per peer
threads = 1                          # Processing threads
batch_size = 64                      # Processing batch size
limiter = 500                        # Rate limit (req/sec)
```

---

## Troubleshooting

### Node Not Syncing

**Symptoms**: Node stuck at low block count, not receiving blocks.

**Diagnosis**:
1. Check if bootstrap is enabled: `"bootstrap": { "enable": true }`
2. Verify network connectivity: Check peer count
3. Check block processor: `block_processor.size()` in stats
4. Look for errors in logs related to bootstrap

**Solutions**:
- Restart node to trigger fresh bootstrap
- Check firewall/NAT settings (TCP port 7075 for live network)
- Verify peers are not all behind same NAT
- Try manually adding representative peers
- Check disk space (ledger database can be 40+ GB)

### Slow Bootstrap

**Symptoms**: Bootstrap progressing but very slowly.

**Diagnosis**:
1. Check block processor throughput: blocks/sec in stats
2. Monitor database I/O: High CPU during batch writes indicates DB bottleneck
3. Check in-flight requests: Should be close to `max_requests`
4. Review peer scoring: Many peers with high outstanding counts indicates network issues

**Solutions**:
- Switch to RocksDB if using LMDB (faster writes)
- Increase `block_processor.batch_size` to 512 or 1024
- Increase `bootstrap.max_requests` to 2048
- Disable `enable_database_scan` (usually not needed)
- Ensure fast disk (SSD recommended)
- Check network bandwidth limits

### Bootstrap Stuck on Dependencies

**Symptoms**: Many accounts in blocking set, not making progress.

**Diagnosis**:
1. Check blocking set size in container_info
2. Look for repeated "gap_source" in logs
3. Check if specific accounts are repeatedly blocked

**Solutions**:
- Verify `enable_dependency_walker = true`
- Check if peer nodes have the missing blocks
- May indicate network-wide issue (missing blocks from failed upgrade)
- Manually prioritize blocked accounts via RPC
- As last resort, clear blocked accounts and rely on frontier scan

### High Memory Usage

**Symptoms**: Node using excessive RAM during bootstrap.

**Diagnosis**:
1. Check priority set size: Should be < 256K accounts
2. Check blocking set size: Should be < 256K accounts
3. Monitor block processor queue

**Solutions**:
- Reduce `priorities_max` to 128K or 64K
- Reduce `blocking_max` to 128K
- Reduce `block_processor.max_depth` in node config
- Disable `enable_database_scan`

### Bootstrap Poisoning

**Symptoms**: Node repeatedly bootstrapping incorrect forks, especially in unconfirmed blocks.

**Diagnosis**:
1. Check if optimistic requests are majority (75% default)
2. Look for blocks with conflicting hashes in logs
3. Check election activity (should be resolving forks)

**Solutions**:
- Reduce `optimistic_request_percentage` to 50 or 25
- Increase safe request percentage (more secure but slower)
- Verify connected to reputable peers
- Elections should automatically resolve forks (check `active_elections` stats)

---

## Developer Guide

### Adding a New Bootstrap Strategy

To add a new bootstrap strategy:

1. **Create strategy class** in `nano/node/bootstrap/`
   ```cpp
   class my_strategy {
       void run();  // Main loop
       void stop(); // Cleanup
   };
   ```

2. **Add to bootstrap_service**:
   ```cpp
   // In bootstrap_service.hpp
   std::unique_ptr<my_strategy> my_strategy_impl;

   // In bootstrap_service.cpp constructor
   my_strategy_impl = std::make_unique<my_strategy>(...);

   // In start()
   threads.push_back(std::thread([this] { my_strategy_impl->run(); }));
   ```

3. **Add configuration**:
   ```cpp
   // In bootstrap_config.hpp
   bool enable_my_strategy = true;
   ```

4. **Implement strategy loop**:
   ```cpp
   void my_strategy::run() {
       while (!stopped) {
           // Select accounts/blocks to bootstrap
           // Call service.run_one(...) to send requests
           // Handle responses via callbacks
       }
   }
   ```

### Testing Bootstrap

**Unit Tests**: `nano/core_test/bootstrap.cpp`
```cpp
TEST (bootstrap, basic) {
    // Create test node
    // Add blocks to peer
    // Trigger bootstrap
    // Verify blocks received
}
```

**Integration Tests**: `nano/slow_test/bootstrap.cpp`
```cpp
TEST (bootstrap_service, sync) {
    // Create two nodes
    // Generate blocks on node1
    // Bootstrap node2 from node1
    // Verify full sync
}
```

**Manual Testing**:
```bash
# Build with tests
NANO_TEST=ON ./ci/build.sh

# Run bootstrap tests
./build/core_test --gtest_filter=bootstrap.*

# Run slow tests
./build/slow_test --gtest_filter=bootstrap_service.*
```

### Debugging Bootstrap

**Enable verbose logging**:
```toml
[node.logging.level]
bootstrap = "trace"
```

**Useful log messages**:
- `"Requesting blocks..."` - Shows requests being sent
- `"Processing response..."` - Shows responses received
- `"Verified blocks..."` - Shows block validation results
- `"Account priority updated..."` - Shows priority changes

**Container Info** (via RPC):
```json
{
  "action": "bootstrap_status"
}
```

Returns:
- In-flight request count
- Priority set size
- Blocking set size
- Strategy-specific stats

**Stats Monitoring**:
```cpp
stats.inc(stat::type::bootstrap_service, stat::detail::request);
stats.inc(stat::type::bootstrap_service, stat::detail::blocks_received);
```

---

## References

### Key Files

**Core Implementation**:
- `nano/node/bootstrap/bootstrap_service.hpp/cpp` - Main client
- `nano/node/bootstrap/bootstrap_server.hpp/cpp` - Server
- `nano/node/bootstrap/account_sets.hpp/cpp` - Priority/blocking management

**Strategies**:
- `nano/node/bootstrap/frontier_scan.hpp/cpp` - Frontier discovery
- `nano/node/bootstrap/database_scan.hpp/cpp` - Database iteration

**Supporting**:
- `nano/node/bootstrap/peer_scoring.hpp/cpp` - Peer selection
- `nano/node/bootstrap/throttle.hpp/cpp` - Throttling logic
- `nano/node/bootstrap/crawlers.hpp` - Database crawlers

**Protocol**:
- `nano/node/messages.hpp` (lines 560-752) - Message definitions

**Tests**:
- `nano/core_test/bootstrap.cpp` - Unit tests
- `nano/slow_test/bootstrap.cpp` - Integration tests

### Related Documentation

- [Nano Protocol Design](https://docs.nano.org/protocol-design/overview/)
- [Node Implementation](https://docs.nano.org/node-implementation/overview/)
- [Running a Node](https://docs.nano.org/running-a-node/overview/)

---

## Conclusion

The Nano bootstrap system is a sophisticated multi-strategy synchronization mechanism designed for the unique requirements of the block-lattice architecture. By running four parallel strategies with intelligent prioritization, dependency resolution, and peer selection, nodes can efficiently synchronize millions of blocks while maintaining security and resource efficiency.

The system balances speed (optimistic requests, high parallelism) with security (safe requests, validation) and resource management (rate limiting, throttling, batching). Understanding these trade-offs is essential for operating and optimizing Nano nodes.
