# Nano Node Voting System

## Overview

Voting is the core mechanism that enables Nano's Open Representative Voting (ORV) consensus protocol. Unlike traditional Proof-of-Work or Proof-of-Stake systems, Nano uses a delegated voting system where account holders delegate their voting weight to representatives who vote on conflicting transactions to reach consensus.

**Key Concepts**:
- **Representatives**: Accounts that vote on behalf of those who delegate to them
- **Voting Weight**: Sum of balances from all accounts delegating to a representative
- **Principal Representatives (PRs)**: Representatives with >0.1% of online voting weight
- **Quorum**: 67% of online voting weight required for consensus
- **Duration-based Voting**: Votes include a validity window encoded in the timestamp
- **Final Votes**: Special votes indicating quorum has been reached

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                         Nano Node                                │
│                                                                  │
│  ┌────────────────────────────────────────────────────────┐    │
│  │              Vote Sources                               │    │
│  │                                                          │    │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────────┐         │    │
│  │  │ Network  │  │   RPC    │  │ Local (Self) │         │    │
│  │  │  Votes   │  │  Votes   │  │   Generated  │         │    │
│  │  └────┬─────┘  └────┬─────┘  └──────┬───────┘         │    │
│  │       └─────────────┼────────────────┘                 │    │
│  └─────────────────────┼──────────────────────────────────┘    │
│                        │                                         │
│                        ▼                                         │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │         Vote Processor (1-4 threads)                     │   │
│  │                                                           │   │
│  │  ┌─────────────────────────────────────┐                │   │
│  │  │  Fair Queue (Priority-based)        │                │   │
│  │  │  • PR Votes (256 max, priority^3)   │                │   │
│  │  │  • Non-PR Votes (32 max, priority=1)│                │   │
│  │  └─────────────┬───────────────────────┘                │   │
│  │                │                                          │   │
│  │                │  Batch (1024 votes)                     │   │
│  │                ▼                                          │   │
│  │  ┌───────────────────────────┐                           │   │
│  │  │  Signature Validation     │                           │   │
│  │  └───────────┬───────────────┘                           │   │
│  └──────────────┼───────────────────────────────────────────┘   │
│                 │                                                │
│                 ▼                                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              Vote Router                                 │   │
│  │                                                           │   │
│  │  • Routes votes to active elections                      │   │
│  │  • Checks recently confirmed cache                       │   │
│  │  • Caches votes without elections                        │   │
│  └─────────────┬───────────────────────────────────────────┘   │
│                │                                                │
│       ┌────────┴────────┐                                       │
│       ▼                 ▼                                       │
│  ┌──────────┐    ┌─────────────┐                               │
│  │  Vote    │    │   Active    │                               │
│  │  Cache   │◄───│  Elections  │                               │
│  │          │    │             │                               │
│  │  64K     │    │  • Tally    │                               │
│  │  blocks  │    │  • Quorum   │                               │
│  │  • Tally │    │  • Confirm  │                               │
│  │  tracking│    └─────────────┘                               │
│  └──────────┘                                                   │
│       ▲                                                          │
│       │                                                          │
│  ┌────┴──────────────────────────────────────────────────┐    │
│  │         Vote Generator                                 │    │
│  │                                                         │    │
│  │  • Regular votes (every 15s)                           │    │
│  │  • Final votes (on quorum)                             │    │
│  │  • Vote spacing (15s cooldown)                         │    │
│  │  • Batch generation (up to 255 hashes)                 │    │
│  └─────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────┘
```

## Vote Structure and Format

### Vote Message

**Location**: `nano/secure/vote.hpp`

```cpp
class vote {
    std::vector<nano::block_hash> hashes;  // Block hashes (max 255)
    nano::account account;                  // Voting representative
    nano::signature signature;              // Ed25519 signature
    uint64_t timestamp_m;                   // Packed timestamp + duration
};
```

**Constraints**:
- Maximum 255 hashes per vote
- All hashes share same timestamp and signature
- Efficient bandwidth usage for voting on multiple blocks

### Timestamp Encoding (Duration-Based Voting)

The timestamp uses the lower 4 bits to encode a duration, creating a validity window:

```
┌─────────────────────────────────────────────┬──────────┐
│         Timestamp (60 bits)                 │ Duration │
│         (milliseconds, 16ms granularity)    │ (4 bits) │
└─────────────────────────────────────────────┴──────────┘
 63                                         4  3        0
```

**Duration Calculation**:
```cpp
duration_ms = 2^(duration_bits + 4)

// Examples:
duration_bits = 0  →  2^4  = 16ms
duration_bits = 5  →  2^9  = 512ms
duration_bits = 10 →  2^14 = 16,384ms
duration_bits = 15 →  2^19 = 524,288ms (~8.7 minutes)
```

**Purpose**:
- Prevents replay attacks (old votes rejected)
- Allows votes to cover a time window
- Representatives can vote on blocks within the duration window without generating new votes

### Regular vs Final Votes

**Regular Votes**:
- Timestamp: Current time with duration encoding
- Default duration: `0x9` (8,192ms = ~8 seconds)
- Indicates representative has seen the block
- Can be superseded by newer votes

**Final Votes**:
- Timestamp: `0xFFFFFFFFFFFFFFFF` (all bits set)
- Indicates representative has observed quorum
- Confirms block is decided
- Cannot be superseded
- Triggers confirmation process

### Vote Signature

**Hash Calculation** (vote.cpp:67-86):
```cpp
nano::block_hash hash() const {
    blake2b_state hash_state;
    blake2b_init(&hash_state, 32);

    // Hash prefix "vote "
    blake2b_update(&hash_state, "vote ", 5);

    // Hash all block hashes
    for (auto const& block_hash : hashes) {
        blake2b_update(&hash_state, block_hash.bytes, 32);
    }

    // Hash timestamp bytes
    blake2b_update(&hash_state, timestamp_bytes, 8);

    blake2b_final(&hash_state, result.bytes, 32);
    return result;
}
```

**Validation**:
```cpp
bool validate() const {
    return nano::validate_message(account, hash(), signature);
}
```

Uses Ed25519 signature verification with the representative's public key.

---

## Vote Processing Pipeline

### 1. Vote Arrival

Votes arrive from three sources:

**Network (Live Traffic)**:
- Received via `confirm_ack` messages
- Gossiped by other nodes
- Highest volume source

**RPC**:
- Manually submitted via `vote` RPC command
- Used for testing or manual intervention

**Local Generation**:
- Generated by node's own wallets
- If node has configured representatives with voting weight

### 2. Vote Processor

**Location**: `nano/node/vote_processor.cpp`

The vote processor uses a **priority-based fair queue** to manage incoming votes.

**Configuration**:
```cpp
max_pr_queue: 256        // Queue size for principal representatives
max_non_pr_queue: 32     // Queue size for non-PRs
pr_priority: 3           // Priority multiplier for PRs
threads: 1-4             // Processing threads (CPU-dependent)
batch_size: 1024         // Votes processed per batch
```

**Priority Calculation**:

Representatives are divided into tiers based on their voting weight:

| Tier | Weight Threshold | Priority | Processing Rate |
|------|-----------------|----------|-----------------|
| Tier 3 | >5% of online stake | priority³ = 27 | 27× |
| Tier 2 | 1-5% of online stake | priority² = 9 | 9× |
| Tier 1 | 0.1-1% of online stake | priority = 3 | 3× |
| None | <0.1% of online stake | 1 | 1× |

**Processing Flow**:
```
1. Vote arrives → Added to fair queue
2. Queue sorts by priority (PR votes processed first)
3. Batch dequeue (up to 1024 votes)
4. Signature validation (in parallel)
5. Pass to vote router
```

**Queue Overflow**:
- PR queue full (256): Drop oldest PR vote
- Non-PR queue full (32): Drop incoming vote
- Stat counters track drops

### 3. Vote Router

**Location**: `nano/node/vote_router.cpp`

Routes votes to their destination (active election or vote cache).

**Processing**:
```cpp
for each hash in vote:
    1. Find active election for hash
    2. If election exists:
       - Pass vote to election
       - Return result (vote/replay/ignored)
    3. If no election:
       - Check recently_confirmed cache
       - If recently confirmed: return late
       - Otherwise: cache vote (return indeterminate)
```

**Vote Codes**:
- `vote`: Successfully processed
- `replay`: Older timestamp (duplicate)
- `ignored`: Valid but rate-limited (cooldown)
- `late`: Election already confirmed
- `indeterminate`: No election found, cached
- `invalid`: Failed signature validation

### 4. Vote Cache

**Location**: `nano/node/vote_cache.cpp`

Stores votes for blocks that don't currently have elections.

**Purpose**:
- Cache votes for blocks before elections start
- Allows quick election startup with existing votes
- Prevents re-requesting votes from network

**Structure**: Boost Multi-Index Container
```cpp
Indices:
1. Hash index     - O(1) lookup by block hash
2. Sequenced      - FIFO ordering for eviction (oldest first)
3. Tally index    - Sorted by weight (descending)
```

**Configuration**:
```cpp
max_size: 65,536 blocks      // 64K entries
max_voters: 64 per block     // Max voters per hash
age_cutoff: 15 minutes       // Automatic cleanup
```

**Per-Block Entry**:
```cpp
{
    hash: block_hash
    voters: up to 64 highest-weight voters
    tally: total voting weight
    final_tally: final voting weight (subset)
    last_vote: timestamp of most recent vote
}
```

**Top Queries**:
- Returns blocks sorted by `final_tally DESC, tally DESC`
- Used by active elections to prioritize which blocks to elect
- Minimum tally threshold filters out low-weight votes

**Eviction**:
- Size-based: Remove oldest when exceeding 64K entries
- Age-based: Remove entries older than 15 minutes
- Voter replacement: If 64 voters exist, replace lowest-weight voter with higher-weight voter

### 5. Active Elections

**Location**: `nano/node/election.cpp`

Manages vote tallying and confirmation for ongoing elections.

**Vote Processing in Elections**:
```
1. Validate representative weight (>= minimum_principal_weight)
2. Check for replay (older timestamp)
3. Check cooldown (non-final votes only):
   - >5% stake: 1 second cooldown
   - 1-5% stake: 5 second cooldown
   - <1% stake: 15 second cooldown
4. Update last_votes map
5. Recalculate tally
6. Check for quorum → Confirm if reached
```

**Vote Storage**:
```cpp
last_votes: map<representative_account, vote_info>
vote_info: {
    time: when vote was received
    timestamp: vote's timestamp
    hash: block hash voted for
}
```

**Tally Calculation**:
```cpp
For each representative in last_votes:
    block_weights[vote_info.hash] += ledger.weight(representative)

    if vote_info.timestamp == FINAL:
        final_weights[vote_info.hash] += ledger.weight(representative)

Sort by weight descending → Return tally
```

**Quorum Check**:
```cpp
bool have_quorum(tally) {
    auto winner_weight = tally[0].weight;
    auto second_weight = tally[1].weight;
    auto delta = online_reps.delta();  // 67% of online weight

    return (winner_weight - second_weight) >= delta;
}
```

Winner must have at least 67% more weight than second place.

---

## Vote Generation

### Vote Generator

**Location**: `nano/node/vote_generator.cpp`

The node has **two vote generators**:

1. **Regular Vote Generator** (`node.generator`)
   - Generates regular votes with duration encoding
   - Used during active elections

2. **Final Vote Generator** (`node.final_generator`)
   - Generates final votes (timestamp = max)
   - Triggered when quorum is observed

**Configuration**:
```cpp
max_queue: 32,768          // Max candidates waiting for votes
batch_size: 256            // Blocks processed per batch
delay: 100ms               // Bundling delay before broadcast
```

### When Votes Are Generated

**Regular Votes**:
1. **Election becomes active**: Initial vote when election transitions from passive to active
2. **Periodic rebroadcast**: Every 15 seconds during election (configurable)
3. **Confirmation requests**: In response to `confirm_req` messages from peers
4. **Manual RPC**: Via `republish` or similar RPC commands

**Final Votes**:
1. **Quorum observed**: When election reaches 67% vote margin
2. **One-time only**: Stored in database, never regenerated for same block

### Vote Generation Process

**Regular Vote Generation**:
```
1. Collect candidate blocks (up to 255)
2. Check vote spacing (15 second cooldown per root)
3. For each wallet representative:
   a. Calculate timestamp with duration (default 0x9 = 8192ms)
   b. Create vote with all candidate hashes
   c. Sign with representative's private key
   d. Store in local vote history
4. Broadcast to network:
   a. Send to local vote processor
   b. Flood to all principal representatives
   c. Flood to 2% of non-principal representatives
```

**Final Vote Generation**:
```
1. Check if block exists and dependencies confirmed
2. Check if final vote already exists in database
3. If not already voted:
   a. Create vote with timestamp = 0xFFFFFFFFFFFFFFFF
   b. Sign with representative's private key
   c. Store in final_vote database table
   d. Broadcast to network
```

### Vote Spacing

**Location**: `nano/node/vote_spacing.cpp`

Prevents generating votes for the same root too frequently.

**Rules**:
- Delay: 15 seconds (production), 1 second (dev network)
- Per-root tracking (not per-hash)
- Can vote immediately if:
  - Voting for same hash as before
  - Delay has elapsed since last vote for this root

**Purpose**:
- Prevents vote spam
- Reduces bandwidth usage
- Allows time for network propagation

### Vote Broadcasting

**Network Propagation**:

```cpp
void broadcast(vote) {
    // 1. Send to local processor (in-process channel)
    vote_processor.vote(vote, inproc_channel);

    // 2. Flood to ALL principal representatives
    //    PRs get every vote for fast consensus
    network.flood_vote_pr(vote);

    // 3. Flood to 2% of non-PRs (random selection)
    //    Prevents vote amplification while maintaining propagation
    network.flood_vote_non_pr(vote, 2.0%);
}
```

**Gossip Strategy**:
- PRs receive all votes (critical for consensus)
- Non-PRs receive sample (prevents network overload)
- Votes from non-PRs are not propagated widely (low weight)

---

## Representative Weights and Consensus

### Weight Calculation

Representative voting weight equals the sum of all account balances that delegate to that representative.

```cpp
nano::uint128_t weight = ledger.weight(representative_account);
```

**Delegation**:
- Every account has a `representative` field
- Representative can be the account itself or any other account
- Representative does NOT control delegated funds (only voting power)
- Delegation can be changed anytime (immediate effect)

### Online Weight Tracking

**Location**: `nano/node/online_reps.cpp`

Tracks which representatives are actively voting to calculate online weight.

**Key Metrics**:

1. **Online Weight** (`online()`):
   - Current sum of weight from recently active representatives
   - Representative is "online" if seen voting within last 5 minutes

2. **Trended Weight** (`trended()`):
   - Median of historical online weight samples
   - More stable than instantaneous online weight
   - Sampled every 5 minutes, stored in database
   - Used for quorum calculations

3. **Delta (Quorum)** (`delta()`):
   - 67% of max(online, trended, minimum)
   - Threshold for consensus

**Observation**:
```cpp
void observe(representative) {
    if (ledger.weight(representative) >= minimum) {
        reps[representative] = current_time;
        update_online_weight();
    }
}
```

Called when vote received from representative.

**Sampling**:
```cpp
Every 5 minutes:
    1. Store current online weight to database
    2. Collect all historical samples (up to 2 weeks)
    3. Calculate median
    4. Update cached_trended
```

**Quorum Calculation**:
```cpp
nano::uint128_t delta() {
    auto max_weight = max({
        cached_online,      // Current online weight
        cached_trended,     // Historical median
        minimum_weight      // Configured minimum (60M NANO default)
    });

    return (max_weight * 67) / 100;  // 67% threshold
}
```

### Principal Representatives

**Definition**: Representatives with ≥0.1% of online voting weight.

**Calculation**:
```cpp
auto minimum_pr_weight = online_reps.trended() / 1000;  // 0.1%

bool is_principal = ledger.weight(rep) >= minimum_pr_weight;
```

**Special Treatment**:
- Larger queue size (256 vs 32)
- Higher processing priority (up to 27×)
- All votes broadcasted to all PRs
- Votes from PRs propagated widely
- Lower cooldown times (1s vs 15s)

### Representative Tiers

**Location**: `nano/node/rep_tiers.cpp`

Representatives categorized into tiers for priority processing:

```cpp
enum class rep_tier {
    none,    // < 0.1% of online stake
    tier_1,  // 0.1% - 1% of online stake
    tier_2,  // 1% - 5% of online stake
    tier_3,  // > 5% of online stake
};
```

**Tier Updates**:
- Recalculated periodically based on current weights
- Used by vote processor for priority assignment
- Affects processing speed and cooldown times

### Rep Crawler

**Location**: `nano/node/repcrawler.cpp`

Actively discovers and tracks representatives by querying the network.

**Process**:
1. Select random confirmed block from ledger
2. Send `confirm_req` to random peer channels
3. Wait for `confirm_ack` (vote) responses
4. Extract representative info from votes
5. Update representative database with channel info

**Modes**:
- **Conservative** (sufficient weight): Query 160 peers, max 4 attempts
- **Aggressive** (insufficient weight): Query 160 peers, max 8 attempts, faster rate

**Purpose**:
- Discover new representatives
- Maintain up-to-date weight information
- Track representative-to-channel mappings
- Ensure sufficient voting weight is online

---

## Consensus Process

### Election Lifecycle

**States**:
1. **Passive**: Election created, listening for votes (no active requests)
2. **Active**: Actively requesting confirmations from network
3. **Confirmed**: Quorum reached, winner decided
4. **Expired**: Election ended (confirmed or unconfirmed)

**State Transitions**:
```
passive (5 base_latency) → active (election_duration) → expired
   ↓                           ↓
   └──────────────┬────────────┘
                  ↓
              confirmed → expired_confirmed
```

**Timeouts**:
- Passive duration: 5× base latency (~5 seconds)
- Active duration (priority): 5 minutes
- Active duration (optimistic/hinted): 30 seconds

### Vote Tallying

**Process**:
```
1. Collect all votes (from last_votes map)
2. For each vote:
   - Look up representative weight
   - Add weight to voted block hash
   - Track final votes separately
3. Sort blocks by total weight (descending)
4. Return sorted tally
```

**Data Structures**:
```cpp
// Per-representative vote info
last_votes: map<account, {timestamp, hash, time}>

// Calculated tally (temporary)
tally: ordered_map<weight, block> (descending)
```

### Quorum and Confirmation

**Quorum Check**:
```
Winner weight - Second weight >= Delta (67% of online weight)

Example:
  Online weight: 100M NANO
  Delta: 67M NANO
  Winner: 70M NANO
  Second: 10M NANO
  Difference: 60M NANO
  Result: 60M < 67M → No quorum

  Winner: 80M NANO
  Second: 10M NANO
  Difference: 70M NANO
  Result: 70M >= 67M → Quorum reached!
```

**Confirmation Process**:
```
1. Calculate tally
2. Check winner stability:
   - If winner changed: force process winning block
3. Check regular quorum (67% margin):
   - If reached AND no final vote sent:
     → Generate final vote
4. Check final vote quorum:
   - Sum final votes for winner
   - If >= delta: Confirm election
5. On confirmation:
   - Update election state
   - Add to recently_confirmed cache
   - Queue to cementing_set
   - Trigger confirmation callbacks
   - Notify observers
```

**Final Vote Requirement**:
- Election generates final vote when quorum observed
- Confirmation requires both:
  - Regular quorum (67% margin)
  - Final vote quorum (67% final votes)
- Ensures consensus is stable before cementing

### Cementing

After confirmation, block must be cemented (confirmation height updated in database).

**Location**: `nano/node/cementing_set.cpp`

**Process**:
```
1. Election confirmed → Add to cementing_set
2. Cementing_set resolves dependencies
3. Batch update confirmation heights (256 per batch)
4. Mark blocks as cemented
5. Notify observers
```

**Dependency Resolution**:
- Can't cement block until all dependencies cemented
- Cementing_set maintains dependency graph
- Processes blocks in topological order

---

## Performance Optimizations

### Batch Processing

**Vote Processor**:
- Batches up to 1024 votes per iteration
- Reduces lock contention
- Improves cache locality
- Single signature validation pass

**Vote Generator**:
- Bundles up to 255 hashes per vote
- 100ms delay to collect candidates
- Reduces signature operations
- Reduces network messages

**Cementing**:
- Batches 256 blocks per transaction
- Reduces database write overhead
- Amortizes transaction costs

### Vote Deduplication

**At Vote Processor**:
- Fair queue prevents duplicate insertions
- Per-representative queue tracking

**At Election**:
- Replay detection by timestamp
- Only newest vote per representative kept
- Older votes automatically discarded

**At Vote Cache**:
- Per-representative tracking (max 64)
- Weight-based replacement (keep highest weight voters)
- Deduplicates votes for same block hash

### Principal Representative Prioritization

**Queue Priority**:
```
Tier 3 (>5%):  priority³ = 27× processing
Tier 2 (1-5%): priority² = 9× processing
Tier 1 (0.1-1%): priority = 3× processing
None (<0.1%):    1× processing
```

**Network Propagation**:
- PR votes: Flood to all PRs (100%)
- Non-PR votes: Flood to 2% of non-PRs

**Cooldowns**:
- >5% stake: 1 second between votes
- 1-5% stake: 5 seconds between votes
- <1% stake: 15 seconds between votes

**Result**: Critical votes (high weight) processed fastest, reaching consensus quickly.

### Lock-Free Operations

**Atomic Counters**:
- Statistics use relaxed atomics (`relaxed_atomic.hpp`)
- No memory barriers for counters
- Reduces contention

**Fair Queue**:
- Minimizes lock time during enqueue/dequeue
- Batch operations under single lock

### Memory Efficiency

**Vote Cache**:
- Multi-index uses shared storage
- Sequenced index for free FIFO eviction
- Maximum 64 voters per block (bounded memory)

**Vote Messages**:
- Batch hashing (up to 255 hashes per vote)
- Shared signature across all hashes
- Compact timestamp encoding

---

## Security Considerations

### Vote Flooding Prevention

**Queue Limits**:
- PR votes: 256 max per queue
- Non-PR votes: 32 max per queue
- Vote cache triggers: 16,384 max

**Result**: Attackers with low voting weight cannot fill queues.

**Network Rate Limiting**:
- Vote cache processor: Max 16,384 triggered lookups
- Overflow: Drop oldest entries
- Prevents memory exhaustion

### Invalid Vote Handling

**Signature Validation**:
```
1. Receive vote
2. Validate signature (Ed25519)
3. If invalid: Reject immediately (no further processing)
4. If valid: Continue to vote router
```

**Weight Filtering**:
```
1. Check representative weight
2. If below minimum_principal_weight: Reject
3. Exception: Dev network (for testing)
```

**Result**: Invalid or low-weight votes filtered early, minimal CPU spent.

### Weight-Based Attack Mitigation

**Minimum Weight Requirement**:
- Elections ignore votes from representatives with <0.1% weight
- Vote cache limits to 64 voters (highest weight kept)
- Rep crawler ignores representatives below minimum

**Cooldown Enforcement**:
- High-weight reps: Short cooldown (1s)
- Low-weight reps: Long cooldown (15s)
- Prevents spam from low-weight accounts

**Quorum Threshold**:
- 67% of online weight required for consensus
- Attacker needs >67% weight to override legitimate consensus
- Significantly higher than 51% attack threshold in PoW

### Replay Protection

**Timestamp Comparison**:
```
1. Check last vote from representative
2. If new timestamp <= old timestamp: Reject (replay)
3. If timestamps equal: Use hash as tiebreaker
4. If new timestamp > old timestamp: Accept
```

**Deterministic Tiebreaker**:
```
If timestamps equal:
    Compare hashes lexicographically
    Keep vote with lower hash value
```

**Final Vote Database**:
- Final votes stored in database
- Cannot vote again with final vote for same root
- Prevents final vote replay

### Cooldown Rate Limiting

**Per-Representative Cooldowns**:
```
>5% stake:  1 second cooldown
1-5% stake: 5 second cooldown
<1% stake:  15 second cooldown
```

**Exemptions**:
- Final votes bypass cooldown
- Allows immediate confirmation when quorum reached

**Purpose**:
- Prevents rapid vote changes
- Reduces network load
- Gives network time to propagate votes

---

## Configuration Reference

### Vote Processor

**File**: `nano/node/vote_processor.hpp`

```toml
[vote_processor]
enable = true                # Master enable/disable
max_pr_queue = 256          # Principal representative queue size
max_non_pr_queue = 32       # Non-principal representative queue size
pr_priority = 3             # Priority multiplier for PRs
threads = 2                 # Processing threads (1-4, CPU-dependent)
batch_size = 1024           # Votes per batch
max_triggered = 16384       # Max vote cache triggers
```

**Defaults**:
- Threads: `clamp(hardware_concurrency / 2, 1, 4)`
- Automatically scales based on CPU cores

### Vote Cache

**File**: `nano/node/vote_cache.hpp`

```toml
[vote_cache]
max_size = 65536            # Maximum cached blocks (64K)
max_voters = 64             # Voters per block
age_cutoff = 900            # Age cutoff in seconds (15 minutes)
```

**Memory Usage**:
- ~64 bytes per voter entry
- ~16 MB for full priority set (256K accounts)
- ~12 MB for full blocking set (256K accounts)
- ~4 MB for vote cache (64K blocks × 64 bytes)

### Vote Generator

**File**: `nano/node/vote_generator.hpp`

```toml
[vote_generator]
max_queue = 32768           # Max candidates (32K)
batch_size = 256            # Blocks per batch
delay = 100                 # Bundling delay in milliseconds
```

### Network Timing

**File**: `nano/lib/constants.hpp`

```toml
[network]
# Production values
aec_loop_interval = 300                 # Active election checking (ms)
vote_broadcast_interval = 15000         # Vote rebroadcast interval (ms)
block_broadcast_interval = 150          # Block rebroadcast interval (ms)
rep_crawler_normal_interval = 7000      # Rep crawler interval (ms)
rep_crawler_warmup_interval = 3000      # Rep crawler warmup (ms)

# Dev network values (faster for testing)
aec_loop_interval = 20
vote_broadcast_interval = 500
block_broadcast_interval = 500
rep_crawler_normal_interval = 500
rep_crawler_warmup_interval = 500
```

### Representative Weights

**File**: `nano/lib/constants.hpp`

```toml
[representatives]
principal_weight_factor = 1000          # 0.1% of online weight (1000 = 1/1000)
online_weight_minimum = "60000000000000000000000000000000000"  # 60M NANO
online_weight_quorum = 67               # 67% threshold
```

**Calculation**:
```cpp
minimum_pr_weight = online_weight / principal_weight_factor
                  = online_weight / 1000
                  = 0.1% of online weight

delta = max(online, trended, minimum) × 0.67
```

### Vote Spacing

**File**: `nano/lib/common.cpp`

```toml
[voting]
# Production
max_cache = 131072          # 128K entries in local vote history
delay = 15                  # 15 second vote spacing

# Dev network
max_cache = 256
delay = 1                   # 1 second vote spacing
```

### Election Timeouts

**File**: `nano/node/election.hpp`

```cpp
// Priority/manual elections
time_to_live = 5 minutes

// Hinted/optimistic elections
time_to_live = 30 seconds

// Passive duration
passive_duration = 5 × base_latency  // ~5 seconds
```

---

## Troubleshooting

### Votes Not Being Processed

**Symptoms**: Node not participating in consensus, vote stats low.

**Diagnosis**:
1. Check if vote processor enabled: `vote_processor.enable = true`
2. Check vote processor queue: Should be processing votes (`stats.vote_processor.process`)
3. Check representative weights: `ledger.weight(representative)` >= minimum
4. Verify network connectivity: Receiving votes from peers

**Solutions**:
- Ensure vote processor enabled in config
- Check wallet unlock status (if generating votes)
- Verify representative has sufficient weight (>0.1% for PR)
- Check firewall (UDP and TCP port 7075)
- Ensure peers are connected

### Elections Not Confirming

**Symptoms**: Blocks stuck in elections, not reaching confirmation.

**Diagnosis**:
1. Check online weight: `online_reps.online()` and `trended()`
2. Check delta: Should be reasonable (67% of online weight)
3. Check election tally: Are votes being received?
4. Check vote distribution: Is there a clear winner?
5. Check final votes: Are final votes being generated?

**Solutions**:
- Wait for sufficient online weight (>60M NANO default)
- Check if fork exists (multiple blocks for same account)
- Verify representatives are online and voting
- Check for network partitions
- If stuck: Manually trigger rebroadcast via RPC

### High Vote Processor Queue

**Symptoms**: Vote processor queues filling up, votes being dropped.

**Diagnosis**:
1. Check queue sizes: `vote_processor.size()`
2. Check drop stats: `vote_processor.overfill`
3. Check processing rate: `vote_processor.process`
4. Check CPU usage: Is node CPU-bound?

**Solutions**:
- Increase `max_pr_queue` and `max_non_pr_queue`
- Increase `vote_processor.threads` (up to 4)
- Reduce `batch_size` for lower latency (trade throughput)
- Upgrade hardware (faster CPU)
- Check for signature verification bottleneck

### Vote Replay Errors

**Symptoms**: Many `vote_code::replay` in stats.

**Diagnosis**:
1. Check timestamp of votes being replayed
2. Check if same representative voting repeatedly
3. Check if votes are old (from cache or delayed network)

**Solutions**:
- Usually benign (old votes naturally rejected)
- If persistent: Check for time synchronization issues
- If from specific peer: May be malicious or misconfigured
- Check local clock (use NTP)

### Insufficient Online Weight

**Symptoms**: Elections not confirming, online weight below threshold.

**Diagnosis**:
1. Check `online_reps.online()` - should be >60M NANO
2. Check `online_reps.trended()` - should be >60M NANO
3. Check number of online representatives
4. Check rep crawler activity

**Solutions**:
- Wait for more representatives to come online
- Check network connectivity (are we isolated?)
- Verify representative discovery (rep crawler)
- Check if major representatives are offline
- Consider lowering `online_weight_minimum` (not recommended for production)

### Vote Spam Attack

**Symptoms**: High vote processor overfill, CPU usage high.

**Diagnosis**:
1. Check which representatives are sending votes
2. Check representative weights
3. Check vote drop stats by tier

**Solutions**:
- Queues automatically limit low-weight votes (32 max)
- Non-PR votes dropped first
- Weight-based filtering active
- No action needed (attack naturally mitigated)
- If persistent: Consider peer banning

---

## Developer Guide

### Generating Votes Programmatically

**Via RPC**:
```json
{
    "action": "republish",
    "hash": "BLOCK_HASH_HERE",
    "count": "1"
}
```

Triggers vote generation for specified block.

**Via Code**:
```cpp
// Add candidate to vote generator
node.generator.add(root, hash);

// Manually generate vote
node.generator.vote({root}, {hash}, [](auto vote) {
    // Broadcast vote
    node.network.flood_vote(vote);
});
```

### Monitoring Vote Activity

**Stats**:
```cpp
// Vote processing
stats.vote_processor.process        // Votes processed
stats.vote_processor.overfill       // Votes dropped (queue full)

// Vote router
stats.vote_router.vote              // Votes routed to elections
stats.vote_router.indeterminate     // Votes cached (no election)
stats.vote_router.late              // Votes for already-confirmed blocks

// Elections
stats.election.vote                 // Votes processed by elections
stats.election.replay               // Replay votes (duplicates)
stats.election.ignored              // Votes ignored (cooldown)
```

**Container Info**:
```cpp
// Via RPC
{
    "action": "stats",
    "type": "objects"
}

Returns:
- vote_processor: queue size
- vote_cache: cache size, entries
- active_elections: election count
```

### Testing Vote Behavior

**Unit Tests**: `nano/core_test/vote_processor.cpp`, `election.cpp`

```cpp
TEST (vote_processor, basic) {
    nano::test::system system;
    auto& node = *system.add_node();

    // Create vote
    auto vote = nano::test::make_vote(rep_key, {block->hash()});

    // Process vote
    node.vote_processor.vote(vote, channel);

    // Verify processing
    ASSERT_TIMELY(5s, node.stats.count(...) == 1);
}
```

**Integration Tests**: `nano/slow_test/election.cpp`

```cpp
TEST (election, quorum) {
    nano::test::system system;
    auto& node1 = *system.add_node();
    auto& node2 = *system.add_node();

    // Set up representatives
    // Generate conflicting blocks
    // Start election
    // Send votes
    // Verify confirmation
}
```

### Debugging Elections

**Enable Verbose Logging**:
```toml
[node.logging.level]
election = "trace"
vote = "trace"
vote_processor = "trace"
```

**Useful Log Messages**:
- `"Processing vote..."` - Vote received
- `"Vote result: ..."` - Vote processing result
- `"Election tally: ..."` - Current tally
- `"Election confirmed"` - Quorum reached

**Inspect Election State** (via debugger):
```cpp
// In election.cpp
auto tally = tally_impl();           // Current vote tally
auto winner = status.winner;         // Current winning block
bool confirmed = is_quorum.load();   // Quorum reached?
auto votes = last_votes.size();      // Number of voting reps
```

### Custom Vote Processing

**Observe Votes**:
```cpp
// Register observer for votes
node.observers.vote.add([](auto vote, auto channel, auto source) {
    // Custom vote processing
    std::cout << "Vote from " << vote->account.to_string()
              << " for " << vote->hashes.size() << " hashes\n";
});
```

**Observe Elections**:
```cpp
// Register observer for confirmations
node.observers.block_confirmed.add([](auto block) {
    std::cout << "Block confirmed: " << block->hash().to_string() << "\n";
});
```

---

## Key Files Reference

### Core Voting Files

**Vote Structure**:
- `nano/secure/vote.hpp` - Vote class definition
- `nano/secure/vote.cpp` - Vote serialization, validation

**Vote Processing**:
- `nano/node/vote_processor.hpp/cpp` - Vote batch processing
- `nano/node/vote_router.hpp/cpp` - Vote routing logic
- `nano/node/vote_cache.hpp/cpp` - Vote caching and aggregation

**Vote Generation**:
- `nano/node/vote_generator.hpp/cpp` - Vote generation logic
- `nano/node/vote_spacing.hpp/cpp` - Vote rate limiting
- `nano/node/local_vote_history.hpp/cpp` - Local vote tracking

**Elections**:
- `nano/node/election.hpp/cpp` - Election state and tallying
- `nano/node/active_elections.hpp/cpp` - Election management

**Representatives**:
- `nano/node/online_reps.hpp/cpp` - Online weight tracking
- `nano/node/rep_tiers.hpp/cpp` - Representative tier calculation
- `nano/node/repcrawler.hpp/cpp` - Representative discovery

### Message Protocol

- `nano/node/messages.hpp` (lines 420-491) - `confirm_ack` message (votes)
- `nano/node/messages.hpp` (lines 303-369) - `confirm_req` message (vote requests)

### Tests

- `nano/core_test/vote_processor.cpp` - Vote processor tests
- `nano/core_test/election.cpp` - Election tests
- `nano/core_test/vote_cache.cpp` - Vote cache tests
- `nano/slow_test/election.cpp` - Integration tests

---

## Conclusion

The Nano voting system implements a sophisticated Open Representative Voting (ORV) consensus mechanism that achieves:

**Fast Consensus**:
- Sub-second confirmation in ideal conditions
- Parallel processing (1-4 threads)
- Principal representative prioritization

**Security**:
- 67% quorum threshold (higher than 51% in PoW)
- Weight-based filtering
- Replay protection
- Vote flooding prevention

**Efficiency**:
- Batch voting (255 hashes per vote)
- Batch processing (1024 votes per batch)
- Vote caching (reduces redundant requests)
- Intelligent gossip (PRs get all votes, non-PRs get 2%)

**Robustness**:
- Duration-based voting (time windows)
- Final votes (stability confirmation)
- Online weight tracking (adapts to network conditions)
- Rep crawler (automatic representative discovery)

Understanding this system is critical for operating Nano nodes, developing applications, and contributing to the protocol.
