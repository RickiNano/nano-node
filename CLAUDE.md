# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is the **Nano Node** - the core implementation of the Nano cryptocurrency protocol. Nano is a digital payment protocol designed for instant, feeless transactions using a unique block-lattice data structure and Open Representative Voting (ORV) consensus mechanism.

**Language**: C++20
**Build System**: CMake 3.14+
**Key Dependencies**: Boost (header-only modules via git submodules), RocksDB, LMDB, cryptopp, Qt5 (for GUI)

## Build Commands

### Standard Build

```bash
# Basic build (creates build/ directory)
./ci/build.sh

# Build with tests enabled
NANO_TEST=ON ./ci/build.sh

# Build with GUI wallet
NANO_GUI=ON ./ci/build.sh

# Build specific target
./ci/build.sh nano_node
```

### Build Configuration Options

Set via environment variables before running `./ci/build.sh`:

- `BUILD_TYPE`: `Debug` (default), `Release`, `RelWithDebInfo`
- `NANO_TEST`: `ON`/`OFF` - Enable test targets
- `NANO_GUI`: `ON`/`OFF` - Enable Qt wallet GUI
- `NANO_NETWORK`: `live` (default), `beta`, `test` - Network to build for
- `SANITIZER`: `ASAN`, `TSAN`, `UBSAN`, `ASAN_INT` - Enable sanitizers
- `NANO_TRACING`: `ON`/`OFF` - Enable trace-level logging (default ON for Debug)
- `COVERAGE`: `ON`/`OFF` - Enable code coverage

### CMake Direct Configuration

```bash
mkdir build && cd build

cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DACTIVE_NETWORK=nano_live_network \
  -DNANO_TEST=ON \
  -DNANO_GUI=OFF \
  ..

cmake --build . --parallel $(nproc)
```

**Key CMake Options**:
- `ACTIVE_NETWORK`: `nano_live_network`, `nano_beta_network`, `nano_test_network`, `nano_dev_network`
- `NANO_TEST`: Build test executables
- `NANO_GUI`: Build Qt wallet
- `NANO_SECURE_RPC`: Enable HTTPS for RPC (requires OpenSSL)
- `NANO_SIMD_OPTIMIZATIONS`: Enable CPU-specific optimizations
- `NANO_ASAN/TSAN/UBSAN`: Enable sanitizers
- `NANO_TIMED_LOCKS`: Mutex timeout debugging (value = ms threshold)
- `NANO_ROCKSDB_TOOLS`: Build RocksDB tools
- `ENABLE_AVX2`: Enable AVX2 optimizations

### Executables Built

After building with `NANO_TEST=ON`, you'll find in `build/`:
- `nano_node` - Main node executable
- `nano_rpc` - Standalone RPC server
- `nano_wallet` - Qt GUI wallet (if `NANO_GUI=ON`)
- `core_test` - Core unit tests
- `rpc_test` - RPC tests
- `slow_test` - Long-running integration tests
- `load_test` - Performance tests

## Testing

### Run All Tests

```bash
cd build
./ci/test.sh
```

This runs: `core_test`, `rpc_test`, `qt_test` (if built), and `systest`

### Run Individual Test Suites

```bash
cd build

# Core unit tests (fastest)
./core_test

# RPC tests
./rpc_test

# Slow integration tests
./slow_test

# Run specific test
./core_test --gtest_filter=block_processor.batch_processing
```

### System Tests

Python-based integration tests in `systest/`:

```bash
cd systest
export NANO_NODE_EXE=../build/nano_node
./RUNALL
```

### Test with Sanitizers

```bash
SANITIZER=ASAN NANO_TEST=ON ./ci/build.sh
cd build && ./core_test
```

## Code Architecture

### Module Organization

The codebase is organized into distinct layers under `nano/`:

#### **nano/lib** - Foundation Layer
Core utilities and data structures used throughout:
- **Block types**: All block structures (send, receive, open, change, state)
- **Crypto primitives**: Account addresses, hashes, signatures (Blake2b, Ed25519)
- **Configuration**: TOML/JSON parsing, network parameters
- **Utilities**: Logging (spdlog), thread pools, timers, work generation, statistics
- **Asio wrappers**: Stream handling, async I/O abstractions

#### **nano/secure** - Ledger & Consensus Layer
Core blockchain and consensus logic:
- **Ledger**: Central class managing blockchain state, transaction processing
- **Block processing**: Visitor pattern for different block types
- **Vote handling**: Vote structures for ORV consensus
- **Representative weights**: Tracking and caching of voting weights
- **Account state**: Account balances, pending transactions

#### **nano/store** - Storage Layer
Database abstraction with two backends:
- **Abstract interface**: `component` class defines storage operations
- **LMDB backend**: Memory-mapped database (traditional)
- **RocksDB backend**: LSM-tree based (better write performance)
- **Tables**: blocks, accounts, pending, confirmation_height, pruned, peers, online_weight, rep_weight, final_vote
- **Transactions**: Read/write transaction management with batching

#### **nano/node** - Node & Network Layer
Main node implementation orchestrating all subsystems:
- **Node class** (`node.hpp`): Central orchestrator with ~50+ components
- **Block Processor**: Async block validation and insertion with 4-tier priority queue
- **Active Elections**: Manages ongoing consensus (max 5000 simultaneous elections)
- **Election Schedulers**: 4 types (hinted, manual, optimistic, priority)
- **Vote Processor/Router/Cache**: Vote validation, routing, and aggregation
- **Network**: TCP-based P2P, peer management, message handling
- **Bootstrap**: Block synchronization for new/recovering nodes
- **Cementing**: Confirmation height update management
- **Wallets**: Key management and transaction creation
- **WebSocket**: Real-time event notifications
- **Telemetry**: Network health monitoring

#### **nano/rpc** - RPC Interface
HTTP-based JSON-RPC API with ~100+ commands for:
- Account queries, block operations, wallet management
- Network statistics, peer information
- Can run in-process or as separate daemon (`nano_rpc`)

### Key Architectural Patterns

#### Block Processing Pipeline
1. **Ingestion**: Blocks from network/RPC/bootstrap
2. **Queue**: 4-tier priority (live/bootstrap/local/system)
3. **Validation**: Signature, work, predecessor checks
4. **Ledger Processing**: Visitor pattern updates state
5. **Election Trigger**: New blocks start consensus
6. **Confirmation**: Once confirmed, blocks are cemented

#### Open Representative Voting (ORV) Consensus
- **Elections**: States: passive → active → confirmed → expired
- **Vote Processing**: Representatives sign block hashes with their voting weight
- **Vote Aggregation**: Votes tallied by representative weight
- **Confirmation**: >50% online rep weight required for quorum
- **Vote Structure**: Timestamp-based with duration encoding, supports batch voting

#### Dependency Injection Pattern
Node components use constructor injection:
```cpp
class node {
    std::unique_ptr<block_processor> block_processor_impl;
    nano::block_processor & block_processor; // Reference for dependencies

    std::unique_ptr<active_elections> active_impl;
    nano::active_elections & active;
    // ... ~50 more components
};
```

#### Visitor Pattern for Block Processing
Different block types (send, receive, open, change, state) processed polymorphically:
```cpp
class ledger_processor : public nano::block_visitor {
    void send_block(nano::send_block const &);
    void receive_block(nano::receive_block const &);
    void state_block(nano::state_block const &);
    // ...
};
```

### Important Conventions

- **Asynchronous by default**: Nearly all I/O uses Boost.Asio
- **RAII everywhere**: Transactions, locks, resources cleaned up automatically
- **Observer pattern**: `observer_set<T>` for event notifications
- **Explicit transactions**: Storage operations require explicit `transaction` objects
- **Stream-based serialization**: Types implement `serialize(stream)` / `deserialize(stream)`
- **Lock-free where critical**: High-performance paths avoid locks when possible
- **Extensive instrumentation**: Stats collection, multi-level logging

### Database Schema

Key tables (in both LMDB and RocksDB):
- **blocks**: Block hash → block data + sideband (account, balance, height, timestamp)
- **accounts**: Account → account_info (head block, rep, open block, balance, height)
- **pending**: Pending key (destination + source hash) → pending_info (source, amount)
- **confirmation_height**: Account → confirmation height (last cemented block height)
- **online_weight**: Timestamp → online voting weight
- **rep_weight**: Representative account → voting weight
- **final_vote**: Root → final vote info

## Development Workflow

### Code Style

- **Formatting**: Uses `.clang-format` for C++ formatting
  - Run: `./ci/clang-format-do.sh` to format all files
  - Check: `./ci/clang-format-check.sh` before committing

- **CMake formatting**:
  - Run: `./ci/cmake-format-do.sh`
  - Check: `./ci/cmake-format-check.sh`

### Common Development Tasks

#### Adding a New RPC Command

1. Add handler in `nano/rpc/handlers/` or extend existing handler
2. Register in `rpc_handler.cpp` command map
3. Add test in `nano/rpc_test/`
4. Update RPC version if breaking change

#### Adding a New Node Component

1. Create class in `nano/node/`
2. Add as member to `node` class in `node.hpp` (unique_ptr + reference)
3. Initialize in `node` constructor with dependency injection
4. Start component in `node::start()`
5. Stop component in `node::stop()`

#### Modifying Block Processing

1. Core logic in `nano/secure/ledger.cpp` (ledger_processor visitor)
2. Block validation in `nano/node/block_processor.cpp`
3. Add tests in `nano/core_test/block_processor.cpp`
4. Consider impact on consensus (elections, vote handling)

#### Adding Storage Tables

1. Define table in appropriate store header (`lmdb/`, `rocksdb/`)
2. Implement in both LMDB and RocksDB backends
3. Add migration logic in `store/versioning.cpp`
4. Bump database version constant
5. Add tests for both backends

### Running a Development Node

```bash
# Build node
./ci/build.sh

# Run with custom data directory
./build/nano_node --data_path /path/to/data

# Run on test network
NANO_NETWORK=test ./ci/build.sh
./build/nano_node --network test

# Enable trace logging
NANO_TRACING=ON ./ci/build.sh
./build/nano_node --config node.enable_voting=true --config node.logging.log_to_cerr=true
```

### Debugging Tips

**Enable detailed logging**:
```toml
# In config-node.toml
[node.logging]
log_to_cerr = true
min_time_between_log_output = 0

[node.logging.level]
ledger = "trace"
election = "trace"
```

**Timed locks debugging**:
```bash
# Detect mutex contention >100ms
cmake -DNANO_TIMED_LOCKS=100 ..
```

**Use sanitizers in debug builds**:
```bash
SANITIZER=ASAN NANO_TEST=ON BUILD_TYPE=Debug ./ci/build.sh
```

**Run specific test with verbose output**:
```bash
./core_test --gtest_filter=ledger.* --gtest_break_on_failure
```

## Network Configurations

- **nano_live_network**: Main production network (default)
- **nano_beta_network**: Beta testing network (V prefix on versions)
- **nano_test_network**: Test network with different parameters
- **nano_dev_network**: Development network (local testing)

Each network has different:
- Genesis account and block
- Epoch blocks and versions
- Work difficulty thresholds
- Peer ports and protocols

## External Documentation

- [Official Docs](https://docs.nano.org)
- [Protocol Design](https://docs.nano.org/protocol-design/overview/)
- [Node Implementation](https://docs.nano.org/node-implementation/overview/)
- [Integration Guides](https://docs.nano.org/integration-guides/the-basics/)
- [RPC Commands](https://docs.nano.org/commands/rpc-protocol/)
- [Whitepaper](https://nano.org/en/whitepaper)

## Important Notes for AI Development

1. **Database operations require transactions**: Always create explicit `transaction` objects for store operations. Use `store.tx_begin_read()` or `store.tx_begin_write()`.

2. **Thread safety**: Most node components run in separate strands. Use `node.workers.post()` or component-specific strands for async operations.

3. **Block validation is multi-stage**: Don't bypass validation steps. Blocks go through: signature → work → ledger processing → election.

4. **Vote weight is time-based**: Online representative weight changes over time. Use appropriate snapshots for calculations.

5. **Test with both backends**: When modifying storage, test both LMDB and RocksDB implementations.

6. **Submodules**: This repo uses git submodules for dependencies (boost, rocksdb, etc.). Remember to update submodules after checkout.

7. **Network-specific code**: Pay attention to `ACTIVE_NETWORK` preprocessor checks for network-specific behavior.

8. **Sanitizers catch issues**: Always test with ASAN/TSAN enabled before submitting changes to core logic.
