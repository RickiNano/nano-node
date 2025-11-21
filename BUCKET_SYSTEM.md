# Nano Node Bucket System

## Overview

The bucket system is a **priority-based election scheduling mechanism** used in the Nano node to organize and schedule blocks for election (consensus voting) based on their account balance. It divides the entire balance range into 63 distinct buckets, where each bucket represents a different balance tier.

### Purpose

The bucket system serves several critical functions:

1. **Fair Election Scheduling** - Prevents high-balance accounts from monopolizing election resources
2. **Resource Management** - Controls memory usage and CPU resources for elections
3. **Priority-Based Confirmation** - Ensures blocks are confirmed based on both account balance and age
4. **Backlog Management** - Helps manage unconfirmed blocks and rollback decisions

## Architecture

### Core Components

The bucket system consists of five main components:

1. **`nano::bucketing`** - Balance-to-bucket mapping logic
2. **`nano::scheduler::priority_pool`** - Shared pool of pending blocks across all buckets
3. **`nano::scheduler::bucket`** - Individual bucket managing elections for a balance tier
4. **`nano::scheduler::priority`** - Main scheduler coordinating bucket operations
5. **`nano::active_elections`** - Active election tracking with bucket metadata

### File Locations

| Component | Header | Implementation |
|-----------|--------|----------------|
| Bucketing | `nano/node/bucketing.hpp` | `nano/node/bucketing.cpp` |
| Priority Pool | `nano/node/scheduler/priority_pool.hpp` | `nano/node/scheduler/priority_pool.cpp` |
| Bucket | `nano/node/scheduler/bucket.hpp` | `nano/node/scheduler/bucket.cpp` |
| Priority Scheduler | `nano/node/scheduler/priority.hpp` | `nano/node/scheduler/priority.cpp` |
| Active Elections | `nano/node/active_elections_index.hpp` | `nano/node/active_elections_index.cpp` |
| Bounded Backlog | `nano/node/bounded_backlog.hpp` | `nano/node/bounded_backlog.cpp` |

## Bucket Structure

### 63 Buckets with Non-Uniform Distribution

The system creates 63 buckets spanning the entire balance range from 0 to 10^120 raw (maximum Nano supply). The distribution is non-uniform to provide more granularity in the middle ranges where most account balances fall.

```cpp
// Bucket 0: 0 balance
// Buckets 1: [2^79, 2^88)          - 1 bucket
// Buckets 2-3: [2^88, 2^92)        - 2 buckets
// Buckets 4-7: [2^92, 2^96)        - 4 buckets
// Buckets 8-15: [2^96, 2^100)      - 8 buckets
// Buckets 16-31: [2^100, 2^104)    - 16 buckets
// Buckets 32-47: [2^104, 2^108)    - 16 buckets
// Buckets 48-55: [2^108, 2^112)    - 8 buckets
// Buckets 56-59: [2^112, 2^116)    - 4 buckets
// Buckets 60-61: [2^116, 2^120)    - 2 buckets
// Bucket 62: [2^120, ∞)            - 1 bucket

Total: 63 buckets
```

### Balance to Bucket Mapping

The `nano::bucketing::bucket_index()` function maps an account balance to a bucket index:

```cpp
nano::bucket_index nano::bucketing::bucket_index (nano::amount balance) const
{
    auto it = std::upper_bound (minimums.begin (), minimums.end (), balance);
    return std::distance (minimums.begin (), std::prev (it));
}
```

**Example Mappings:**
- 0 raw → Bucket 0
- 1 nano (10^30 raw) → Bucket 14
- 1 Knano (10^33 raw) → Bucket 49
- Maximum supply → Bucket 62

## Priority Calculation

Each block is assigned a two-part priority used for scheduling:

### 1. Priority Balance

The balance used to determine the bucket. For most blocks, this is simply the account balance. However, for send blocks with a final balance of 0, the previous balance is used to ensure fair priority.

```cpp
auto const priority_balance = std::max (balance, block.is_send () ? previous_balance : 0);
```

### 2. Priority Timestamp

Used for ordering within a bucket. The timestamp of the previous block is used (if available) to implement LRU (Least Recently Used) ordering - older blocks get priority.

```cpp
auto const priority_timestamp = previous_block
    ? previous_block->sideband ().timestamp
    : block.sideband ().timestamp;
```

### Priority Ordering

- **Inter-bucket**: Higher balance → Higher bucket index → Scheduled first
- **Intra-bucket**: Lower timestamp → Older block → Higher priority

## Data Structures

### Priority Entry

Represents a block in the priority pool or bucket:

```cpp
struct priority_entry
{
    std::shared_ptr<nano::block> block;
    nano::bucket_index bucket;              // 0-62
    nano::priority_timestamp priority;       // For LRU ordering
};
```

### Priority Pool

A shared pool of blocks waiting to be elected, organized by bucket. Uses Boost Multi-Index Container with three indices:

- **`tag_bucket_priority`**: Ordered by (bucket DESC, priority ASC, hash ASC)
- **`tag_hash`**: Hashed by block hash for O(1) lookup
- **`tag_priority`**: Ordered by priority for eviction decisions

### Scheduler Bucket

Manages active elections for a single balance tier. Uses Boost Multi-Index Container with:

- **`tag_sequenced`**: Insertion order
- **`tag_root`**: Hashed by qualified root for O(1) lookup
- **`tag_priority`**: Ordered by priority (descending) for cleanup

## Election Lifecycle

### 1. Block Activation → Priority Pool

When a block needs to be confirmed, it enters the priority pool:

```cpp
bool nano::scheduler::priority::activate (std::shared_ptr<nano::block> const & block)
{
    // Calculate priority
    auto const [priority_balance, priority_timestamp] = ledger.block_priority (transaction, *block);

    // Determine bucket
    auto const bucket_index = bucketing.bucket_index (priority_balance);

    // Add to pool
    bool added = pool.lock ()->push (block, bucket_index, priority_timestamp);

    return added;
}
```

### 2. Priority Pool → Bucket Election

The scheduler periodically runs and attempts to activate elections:

```cpp
void nano::scheduler::priority::run ()
{
    // Get highest priority block from each bucket
    auto tops = pool.lock ()->top_all ();

    // Try to activate each one
    for (auto const & [index, top] : tops)
    {
        auto const & bucket = buckets.at (index);
        if (bucket->available (top))
        {
            bucket->activate (top);  // Start election
            activated.push_back (top.block->hash ());
        }
    }

    // Remove activated blocks from pool
    pool.lock ()->erase_all (activated);
}
```

### 3. Bucket → Active Elections

The bucket starts an election by inserting it into the active elections container:

```cpp
bool nano::scheduler::bucket::activate (priority_entry top)
{
    auto result = active.insert (top.block,
                                 nano::election_behavior::priority,
                                 index,           // bucket index
                                 top.priority,    // timestamp
                                 erase_callback);

    if (result.inserted)
    {
        elections.insert ({ result.election,
                          result.election->qualified_root,
                          top.priority });
    }

    return result.inserted;
}
```

### 4. Election Completion

When an election completes (block is confirmed or dropped), the bucket's cleanup callback is invoked to remove it from tracking.

## Capacity Management

### Configuration

Each bucket has configurable capacity limits:

```cpp
struct priority_config
{
    size_t max_blocks{ 1024 * 64 };        // 64K blocks total across all buckets
    size_t reserved_blocks{ 1024 * 8 };    // 8K blocks guaranteed per bucket
    size_t reserved_elections{ 100 };      // 100 elections guaranteed per bucket
    size_t max_elections{ 150 };           // 150 max elections per bucket
};
```

### Activation Logic

A bucket will activate a new election if:

1. **Below Reserved Capacity**: Always activate if `elections.size() < reserved_elections`
2. **Below Max Capacity with Vacancy**: Activate if `elections.size() < max_elections` AND there's vacancy in the active elections container
3. **Priority Replacement**: If at max capacity, only activate if the new block has better priority than the worst current election

```cpp
bool nano::scheduler::bucket::activate_predicate (nano::priority_timestamp candidate) const
{
    if (elections.size () < config.reserved_elections)
    {
        return true;  // Always activate within reserved capacity
    }

    if (elections.size () < config.max_elections)
    {
        return active.vacancy (nano::election_behavior::priority) > 0;
    }

    // At max capacity, check if candidate is better than worst
    if (!elections.empty ())
    {
        auto worst_it = by_priority.begin ();
        if (candidate < worst_it->priority)
        {
            return elections.size () < config.max_elections * 2;
        }
    }

    return false;
}
```

### Cleanup and Eviction

If a bucket becomes overfilled (due to external insertions), it will cancel the lowest priority election:

```cpp
bool nano::scheduler::bucket::cleanup ()
{
    if (overfill_predicate ())
    {
        // Cancel the election with worst priority
        return cancel_lowest_election ();
    }
    return false;
}
```

## Round-Robin Scheduling

The priority scheduler implements a round-robin approach across buckets:

1. For each run, it attempts to get the top block from **every** bucket
2. It tries to activate one election from each bucket (if available)
3. This ensures fair distribution of election resources
4. Higher buckets may fill faster due to AEC vacancy, but lower buckets are never starved

## Integration with Other Systems

### Bounded Backlog

The `bounded_backlog` component uses buckets to manage unconfirmed blocks:

- Tracks unconfirmed block count per bucket
- Implements per-bucket thresholds for rollback decisions
- Helps maintain ledger health by rolling back low-priority stuck blocks

### Active Elections Container (AEC)

The active elections container tracks all ongoing elections:

- Maintains bucket metadata for each election
- Provides vacancy information to buckets
- Implements global election limits across all election behaviors

### Cementing

When blocks are cemented (permanently confirmed):

- Successor blocks are automatically activated
- Uses `notify_cemented` observers to trigger activation
- Ensures dependent blocks can progress

## Thread Safety

The bucket system is designed for concurrent access:

- **Mutexes**: Protect shared data structures
- **Condition Variables**: Enable efficient waiting in scheduler threads
- **Lock Guards**: RAII-style mutex management
- **Atomic Operations**: Used where appropriate for lock-free reads

## Configuration

All bucket system parameters can be configured via TOML:

```toml
[node.scheduler.priority]
max_blocks = 65536              # Total pool capacity
reserved_blocks = 8192          # Reserved per bucket
reserved_elections = 100        # Guaranteed elections per bucket
max_elections = 150             # Max elections per bucket
enable = true                   # Enable/disable priority scheduler
cleanup_interval_ms = 100       # Cleanup interval
```

## Election Behaviors

The Nano node supports multiple election behaviors:

| Behavior | Description | Bucket System |
|----------|-------------|---------------|
| `priority` | Balance-based priority elections | Managed by bucket system |
| `manual` | Manually triggered elections | Not bucketed |
| `hinted` | Vote-driven elections | Not bucketed |
| `optimistic` | Optimistic confirmation | Not bucketed |

Only `priority` behavior elections are managed by the bucket system.

## Key Functions Reference

### Balance to Bucket

**File**: `nano/node/bucketing.cpp:35-41`

```cpp
nano::bucket_index nano::bucketing::bucket_index (nano::amount balance) const
```

Maps an account balance to a bucket index (0-62).

### Block Priority

**File**: `nano/ledger/ledger.cpp:795-808`

```cpp
auto nano::ledger::block_priority (nano::secure::transaction const & transaction,
                                   nano::block const & block) const -> block_priority_result
```

Calculates the priority balance and timestamp for a block.

### Pool Push

**File**: `nano/node/scheduler/priority_pool.cpp`

```cpp
bool nano::scheduler::priority_pool::push (std::shared_ptr<nano::block> const & block,
                                           nano::bucket_index bucket,
                                           nano::priority_timestamp priority)
```

Adds a block to the priority pool.

### Pool Top

**File**: `nano/node/scheduler/priority_pool.cpp`

```cpp
std::optional<priority_entry> nano::scheduler::priority_pool::top (nano::bucket_index index)
```

Gets the highest priority block for a specific bucket.

### Bucket Activate

**File**: `nano/node/scheduler/bucket.cpp:86-120`

```cpp
bool nano::scheduler::bucket::activate (priority_entry top)
```

Starts an election for a block from the priority pool.

### Scheduler Run

**File**: `nano/node/scheduler/priority.cpp:228-272`

```cpp
void nano::scheduler::priority::run ()
```

Main scheduler loop that activates elections from the priority pool.

## Performance Considerations

### Memory Usage

- Priority pool: ~64K blocks × ~300 bytes = ~19 MB maximum
- Per-bucket elections: 63 buckets × 150 elections × 8 bytes = ~75 KB
- Multi-index overhead: Additional ~50% for index structures

### CPU Usage

- Scheduler runs every 100ms (configurable)
- Cleanup runs every 100ms
- Multi-index operations: O(log n) for insertions, O(1) for hash lookups
- Bucket index calculation: O(log 63) = O(1) effectively

### Optimization Techniques

1. **Shared Pool**: Unused capacity in one bucket can be used by others
2. **Multi-Index Containers**: Efficient lookups and ordering
3. **Lock-Free Reads**: Where possible using atomic operations
4. **Lazy Cleanup**: Only runs when necessary
5. **Batch Operations**: Multiple activations per scheduler run

## Debugging

### Statistics

The bucket system exposes statistics:

```cpp
nano::scheduler::priority::stats
{
    size_t activated;         // Blocks activated
    size_t overfill;          // Overfill cleanups
    size_t prioritized;       // Blocks added to pool
}
```

### Observables

The system provides observers for monitoring:

- `election_started` - Fired when an election begins (includes bucket and priority)
- `block_cemented` - Triggers successor activation

### Logging

Enable debug logging for detailed information:

```toml
[node.logging]
priority_scheduler_debug = true
```

## Common Scenarios

### High-Balance Account Transfer

1. Block arrives with balance 1M Nano
2. Priority calculation: `priority_balance = 1M Nano`, `priority_timestamp = previous_block_timestamp`
3. Bucket index calculation: Maps to bucket ~55
4. Added to priority pool
5. Scheduler runs, activates from bucket 55 (high priority bucket)
6. Election starts immediately if bucket has capacity
7. Confirmed quickly due to high balance priority

### Low-Balance Account Transfer

1. Block arrives with balance 1 Nano
2. Priority calculation: `priority_balance = 1 Nano`, `priority_timestamp = previous_block_timestamp`
3. Bucket index calculation: Maps to bucket 14
4. Added to priority pool
5. Scheduler runs round-robin across all buckets
6. Activated when bucket 14's turn comes (guaranteed fair access)
7. May take longer than high-balance transfers but not starved

### Full Send Block

1. Send block reduces balance to 0
2. Priority calculation uses previous balance: `priority_balance = max(0, previous_balance)`
3. Bucket determined by sender's balance before send
4. Prevents zero-balance sends from being deprioritized unfairly

## Summary

The bucket system is a sophisticated mechanism that balances:

- **Fairness**: All account sizes get proportional access to elections
- **Performance**: Efficient data structures and algorithms
- **Resource Management**: Controlled memory and CPU usage
- **Flexibility**: Highly configurable parameters

It ensures that the Nano network can confirm transactions efficiently while maintaining decentralization and preventing resource monopolization by high-balance accounts.
