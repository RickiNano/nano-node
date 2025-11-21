# Nano Node Benchmarks

## Overview

The Nano node includes built-in benchmarks to measure different aspects of performance. These benchmarks test various subsystems in isolation or as part of the full confirmation pipeline.

## Available Benchmarks

The node has **4 benchmark types** you can run:

| Benchmark | Purpose | File |
|-----------|---------|------|
| `--benchmark_block_processing` | Tests raw block validation and ledger write performance | `nano/nano_node/benchmarks/benchmark_block_processing.cpp` |
| `--benchmark_cementing` | Tests block cementing throughput | `nano/nano_node/benchmarks/benchmark_cementing.cpp` |
| `--benchmark_elections` | Tests election and voting system performance | `nano/nano_node/benchmarks/benchmark_elections.cpp` |
| `--benchmark_pipeline` | Tests the full confirmation pipeline | `nano/nano_node/benchmarks/benchmark_pipeline.cpp` |

## Basic Usage

```bash
nano_node --benchmark_block_processing
```

## Configuration Options

All benchmarks support these command-line options:

| Option | Default | Description |
|--------|---------|-------------|
| `--accounts <number>` | 150,000 | Number of accounts to generate |
| `--iterations <number>` | 5 | Number of benchmark iterations |
| `--batch_size <number>` | 250,000 | Number of blocks per iteration |
| `--cementing_mode <mode>` | sequential | Cementing mode: `sequential` or `root` |

## Examples

### Basic block processing benchmark

```bash
nano_node --benchmark_block_processing
```

### Customize parameters

```bash
nano_node --benchmark_block_processing --accounts 100000 --iterations 10 --batch_size 500000
```

### Elections benchmark with custom settings

```bash
nano_node --benchmark_elections --accounts 50000 --batch_size 100000 --iterations 3
```

### Cementing benchmark with root mode

```bash
nano_node --benchmark_cementing --cementing_mode root --batch_size 100000
```

### Full pipeline benchmark

```bash
nano_node --benchmark_pipeline --accounts 200000 --iterations 5
```

## Benchmark Details

### Block Processing Benchmark

**Command**: `--benchmark_block_processing`

**What it tests:**
- Block validation speed (signature verification, balance checks, epoch validation)
- Ledger write performance (database insertion)
- Block processor queue management
- Unchecked block handling for out-of-order blocks

**How it works:**
1. **Setup**: Creates a node with unlimited queue sizes and disabled work requirements
2. **Generate**: Creates random transfer transactions (send/receive pairs) between accounts
3. **Submit**: Adds all blocks to the block processor queue via `block_processor.add()`
4. **Measure**: Tracks time from submission until all blocks are processed into the ledger
5. **Report**: Calculates blocks/sec throughput and final account states

**Metrics reported:**
- Blocks/second throughput
- Total processing time
- Blocks processed successfully
- Blocks failed (with reasons: old, gap_previous, gap_source)
- Account distribution statistics

**Does NOT test:**
- Elections or voting (blocks are not confirmed)
- Cementing (blocks remain unconfirmed)
- Network communication (local-only testing)

**Use cases:**
- Measuring raw block processing capacity
- Testing database write performance
- Validating block processor improvements
- Comparing LMDB vs RocksDB performance

---

### Cementing Benchmark

**Command**: `--benchmark_cementing`

**What it tests:**
- Confirmation height processor performance
- Sequential vs. root cementing modes
- Database write performance for confirmation updates
- Cementing throughput under different dependency structures

**How it works:**
1. **Setup**: Creates a node and generates blocks
2. **Process**: Inserts blocks into the ledger
3. **Cement**: Uses confirmation height processor to cement blocks
4. **Measure**: Tracks time from cementing request to completion
5. **Report**: Calculates cementing throughput

**Cementing Modes:**

**Sequential Mode** (`--cementing_mode sequential`):
- Cements blocks in sequential order
- Tests typical confirmation pattern
- Each block is cemented independently

**Root Mode** (`--cementing_mode root`):
- Creates dependency tree structure
- All blocks converge to a single root block
- Tests root-based cementing cascade
- Simulates optimized confirmation patterns

**Metrics reported:**
- Blocks cemented per second
- Total cementing time
- Confirmation height updates
- Database write performance

**Use cases:**
- Testing confirmation height processor performance
- Comparing cementing strategies
- Validating cementing optimizations
- Stress testing confirmation updates

---

### Elections Benchmark

**Command**: `--benchmark_elections`

**What it tests:**
- Election startup performance
- Vote generation and processing speed (with local representative)
- Quorum detection and confirmation logic
- Cementing after confirmation
- Concurrent election handling

**How it works:**
1. **Setup**: Creates a node with genesis representative key for voting
2. **Prepare**: Generates independent open blocks (send blocks are pre-cemented)
3. **Process**: Inserts open blocks directly into ledger (bypassing block processor)
4. **Start**: Manually triggers elections for all open blocks
5. **Measure**: Tracks time from election start until blocks are confirmed and cemented
6. **Report**: Calculates election throughput and timing statistics

**Metrics reported:**
- Elections started
- Elections stopped
- Elections confirmed
- Blocks cemented
- Time to confirmation
- Time to cementing

**Does NOT test:**
- Block processing (blocks inserted directly)
- Network vote propagation (local voting only)
- Election schedulers (elections started manually)

**Use cases:**
- Measuring election subsystem capacity
- Testing vote processing performance
- Validating quorum detection
- Comparing active election container (AEC) implementations

---

### Pipeline Benchmark

**Command**: `--benchmark_pipeline`

**What it tests:**
- Full end-to-end confirmation pipeline
- Combined block processing + elections + cementing
- Real-world workflow simulation
- Integration of all subsystems

**How it works:**
1. **Setup**: Creates a fully configured node with all subsystems enabled
2. **Generate**: Creates realistic transaction patterns
3. **Process**: Blocks go through normal processing pipeline
4. **Confirm**: Elections run automatically via schedulers
5. **Cement**: Confirmation height processor handles cementing
6. **Measure**: Tracks timing through entire pipeline
7. **Report**: Overall throughput and stage-by-stage timing

**Metrics reported:**
- Overall blocks/second throughput
- Time in each pipeline stage
- End-to-end latency
- Resource utilization

**Use cases:**
- Measuring real-world performance
- Testing complete node capacity
- Identifying pipeline bottlenecks
- Validating end-to-end improvements

---

## Output Example

When you run a benchmark, you'll see output like:

```
=== BENCHMARK: Block Processing ===
Configuration:
  Accounts: 150000
  Iterations: 5
  Batch size: 250000

System Info:
  Backend: lmdb
  Block processor threads: 1
  Block processor batch size: 65536

Generating 150000 accounts...
Setting up genesis distribution...
Genesis distribution complete: 100.0% distributed, 0.0% retained for voting

--- Iteration 1/5 --------------------------------------------------------------
Generating 125000 random transfers...
Generated 250000 blocks
Processing 250000 blocks...
Blocks remaining:         0 (block processor:         0 | unchecked:     0)

Performance: 15234 blocks/sec [16.41s] 250000 blocks processed
─────────────────────────────────────────────────────────────────

--- Iteration 2/5 --------------------------------------------------------------
...

--- SUMMARY ---------------------------------------------------------------------

Blocks processed:             1250000
Blocks failed:                      0
Blocks old:                         0
Blocks gap_previous:                0
Blocks gap_source:                  0

Accounts total:                150000
Accounts with balance:          75234 (50.2%)
```

## Performance Tips

### For Accurate Results

1. **Run multiple iterations**: Use `--iterations 10` or more for consistent results
2. **Warm up the system**: First iteration may be slower due to initialization
3. **Close other applications**: Minimize background processes
4. **Use consistent parameters**: Keep settings identical across test runs
5. **Clean data directory**: Each run uses a temporary directory for isolation

### For Quick Tests

1. **Reduce batch size**: `--batch_size 10000` for faster feedback
2. **Fewer accounts**: `--accounts 10000` reduces setup time
3. **Single iteration**: `--iterations 1` for quick sanity checks

### For Stress Testing

1. **Large batch sizes**: `--batch_size 1000000` tests maximum capacity
2. **Many accounts**: `--accounts 500000` simulates real network scale
3. **Multiple iterations**: `--iterations 20` validates sustained performance

### Resource Considerations

**Memory Usage:**
- Batch size directly affects RAM usage
- Large batches (500K+) can use several GB of RAM
- Monitor system memory during tests

**Disk I/O:**
- Database backend affects performance significantly
- LMDB: Memory-mapped, benefits from large RAM
- RocksDB: More disk I/O, benefits from fast SSD

**CPU Usage:**
- All cores will be utilized during benchmarks
- Signature verification is CPU-intensive
- Higher thread counts improve throughput

## Interpreting Results

### Block Processing Benchmark

**Good Performance:**
- 10,000+ blocks/sec on modern hardware
- Zero failed blocks
- Low gap_previous/gap_source counts

**Bottlenecks to investigate:**
- Low blocks/sec: Database write speed, CPU for signature verification
- High gap counts: Out-of-order block handling, need larger unchecked cache
- Failed blocks: Logic errors, need investigation

### Cementing Benchmark

**Good Performance:**
- Fast cementing relative to block processing
- Sequential mode: Should handle thousands of blocks/sec
- Root mode: Can cement entire tree in single operation

**Bottlenecks to investigate:**
- Slow cementing: Database write performance
- Mode differences: Confirmation height algorithm efficiency

### Elections Benchmark

**Good Performance:**
- Fast election startup (milliseconds per election)
- Quick confirmation with local representative (genesis key)
- Efficient cementing after confirmation

**Bottlenecks to investigate:**
- Slow election startup: Active elections container (AEC) performance
- Delayed confirmation: Vote processing bottleneck
- Slow cementing: Confirmation height processor

### Pipeline Benchmark

**Good Performance:**
- High overall throughput (5,000+ blocks/sec)
- Balanced performance across all stages
- No single bottleneck stage

**Bottlenecks to investigate:**
- Low throughput: Identify slowest stage
- Unbalanced stages: Optimize bottleneck component
- Resource exhaustion: Scale resources or adjust limits

## Benchmark Implementation

### Architecture

**Base Class**: `nano::cli::benchmark_base` (`nano/nano_node/benchmarks/benchmarks.hpp`)

Provides common functionality:
- **Account pool**: Manages keypairs, balances, and frontiers
- **Block generation**: Creates various transaction patterns
- **Genesis distribution**: Sets up initial state

**Generation Methods:**

1. **`generate_random_transfers()`**
   - Creates random send/receive pairs
   - No specific dependency structure
   - Simulates typical network activity

2. **`generate_dependent_chain()`**
   - Creates dependency tree structure
   - All blocks converge to single root
   - Optimized for root mode cementing

3. **`generate_independent_blocks()`**
   - Creates one block per account
   - No dependencies between blocks
   - Ideal for concurrent processing

### Configuration

**Default Configuration:**
```cpp
struct benchmark_config {
    size_t num_accounts = 150000;
    size_t num_iterations = 5;
    size_t batch_size = 250000;
    cementing_mode cementing_mode = cementing_mode::sequential;
};
```

**Node Configuration:**
- Work thresholds set to 0 (no PoW required)
- Unlimited block processor queue
- Large unchecked blocks cache (1M blocks)
- Random port (no network peering)
- Disabled bounded backlog

## Data Storage

**Temporary Data Directories:**
- Each benchmark run creates a unique temporary directory
- Uses `nano::unique_path()` for isolation
- No data persists after benchmark completion
- Safe to run multiple benchmarks simultaneously

**Cleanup:**
- Automatic cleanup on benchmark exit
- No manual cleanup required

## Extending Benchmarks

### Adding a New Benchmark

1. Create new `.cpp` file in `nano/nano_node/benchmarks/`
2. Inherit from `benchmark_base` class
3. Implement benchmark logic
4. Add entry point function
5. Register in `entry.cpp`:

```cpp
("benchmark_my_feature", "Description of benchmark")
```

### Adding Metrics

Use atomic counters for thread-safe metrics:

```cpp
std::atomic<size_t> my_metric{ 0 };
my_metric++; // Thread-safe increment
```

Use locked containers for complex data:

```cpp
nano::locked<std::unordered_map<...>> my_data;
auto locked = my_data.lock();
locked->insert(...);
```

### Adding Notifications

Subscribe to node events:

```cpp
node->ledger_notifications.blocks_processed.add([this](auto const & batch) {
    // Handle block processing events
});

node->observers.election_started.add([this](auto const & election) {
    // Handle election start events
});

node->observers.block_cemented.add([this](auto const & block) {
    // Handle cementing events
});
```

## Troubleshooting

### Benchmark Fails to Start

**Symptoms:** Crashes or errors during initialization

**Solutions:**
- Check available disk space
- Verify write permissions
- Ensure port availability (if specified)
- Check memory availability

### Inconsistent Results

**Symptoms:** Large variance between iterations

**Solutions:**
- Increase iteration count (`--iterations 20`)
- Close background applications
- Check for thermal throttling
- Verify stable system load

### Out of Memory

**Symptoms:** Node crashes during large batches

**Solutions:**
- Reduce batch size (`--batch_size 100000`)
- Reduce account count (`--accounts 50000`)
- Increase system swap space
- Use machine with more RAM

### Slow Performance

**Symptoms:** Much slower than expected

**Solutions:**
- Check database backend (LMDB vs RocksDB)
- Verify SSD storage (not HDD)
- Check CPU governor (performance mode)
- Disable debug builds (use release builds)

### Database Errors

**Symptoms:** LMDB or RocksDB errors

**Solutions:**
- Ensure sufficient disk space
- Check filesystem permissions
- Verify database isn't locked by another process
- Try different backend (`--rocksdb.enable=true`)

## Comparing Backends

### LMDB vs RocksDB Comparison

Run the same benchmark with both backends:

**LMDB (default):**
```bash
nano_node --benchmark_block_processing --accounts 100000 --batch_size 250000
```

**RocksDB:**
```bash
nano_node --benchmark_block_processing --accounts 100000 --batch_size 250000 \
  --config node.rocksdb.enable=true
```

**Expected Differences:**
- LMDB: Faster reads (zero-copy), slower writes
- RocksDB: Faster writes (LSM-tree), smaller size (~65% of LMDB)
- LMDB: Benefits from large RAM
- RocksDB: Benefits from fast SSD

## CI/CD Integration

### Automated Performance Testing

Run benchmarks in CI pipeline:

```bash
#!/bin/bash
# Run benchmark and capture output
nano_node --benchmark_block_processing \
  --accounts 50000 \
  --batch_size 100000 \
  --iterations 5 > benchmark_results.txt

# Parse results
THROUGHPUT=$(grep "blocks/sec" benchmark_results.txt | awk '{print $2}')

# Check threshold (example: minimum 8000 blocks/sec)
if [ "$THROUGHPUT" -lt 8000 ]; then
  echo "Performance regression detected: $THROUGHPUT blocks/sec"
  exit 1
fi
```

### Performance Tracking

Store results over time to track performance trends:

```bash
# Append results with timestamp
echo "$(date +%s) $THROUGHPUT" >> performance_history.csv
```

## Summary

The Nano node benchmarks provide comprehensive performance testing across all critical subsystems:

- **Block Processing**: Raw validation and ledger write performance
- **Cementing**: Confirmation height processor throughput
- **Elections**: Voting and consensus performance
- **Pipeline**: End-to-end real-world performance

Use these benchmarks to:
- Measure node performance
- Compare hardware configurations
- Validate optimizations
- Identify bottlenecks
- Test database backends

For best results, run multiple iterations with realistic parameters and analyze trends across runs.
