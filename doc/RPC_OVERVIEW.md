# RPC System Overview

The Nano Node RPC (Remote Procedure Call) system provides an HTTP-based JSON-RPC API for interacting with the node. This document provides an architectural overview of how the RPC system works internally.

## Architecture

The RPC system is built on several layers that work together to process requests:

```
Client Request (HTTP/JSON)
         ↓
    nano::rpc (HTTP Server)
         ↓
  nano::rpc_connection (Request Handler)
         ↓
  nano::rpc_handler (Request Parser)
         ↓
  nano::rpc_handler_interface (Abstraction)
         ↓
  Implementation (in-process or IPC)
         ↓
  nano::json_handler (Command Router)
         ↓
  Command Implementation (100+ handlers)
```

## Deployment Modes

The RPC system supports two distinct deployment modes:

### 1. In-Process Mode

The RPC server runs within the same process as the node:

- **Handler**: `nano::inprocess_rpc_handler`
- **Benefits**: Direct access to node internals, lower latency
- **Use case**: Single-process deployment, development, testing
- **Location**: `nano/node/json_handler.hpp`

### 2. Standalone Daemon Mode

The RPC server runs as a separate process (`nano_rpc`):

- **Handler**: `nano::ipc_rpc_processor`
- **Communication**: IPC (Inter-Process Communication) over TCP
- **Benefits**: Process isolation, can restart RPC without node
- **Use case**: Production deployments requiring separation
- **Location**: `nano/nano_rpc/entry.cpp`
- **Executable**: `build/nano_rpc`

In daemon mode, the `nano_rpc` process connects to the node via IPC and forwards requests. The node must have IPC enabled for this to work.

## Core Components

### nano::rpc (nano/rpc/rpc.hpp)

The HTTP server that listens for incoming connections:

- Uses Boost.Asio for async I/O
- Manages TCP acceptor and connection lifecycle
- Delegates request handling to `rpc_connection`
- Supports both HTTP and HTTPS (with `NANO_SECURE_RPC`)

### nano::rpc_connection (nano/rpc/rpc_connection.hpp)

Handles individual HTTP connections:

- Uses Boost.Beast for HTTP protocol handling
- Parses HTTP headers and body
- Extracts request parameters (action, body, credentials, correlation_id)
- Supports both RPC v1 and v2 protocols
- Delegates to `rpc_handler` for processing

### nano::rpc_handler (nano/rpc/rpc_handler.hpp)

Validates and pre-processes requests:

- Validates JSON depth limits (`max_json_depth`)
- Checks request size limits (`max_request_size`)
- Enforces control command restrictions (see Security)
- Parses the `action` field from JSON
- Delegates to the configured `rpc_handler_interface` implementation

### nano::rpc_handler_interface (nano/lib/rpc_handler_interface.hpp)

Abstract interface with two implementations:

- **`inprocess_rpc_handler`**: Processes requests in-process
- **`ipc_rpc_processor`**: Forwards requests via IPC to node

This abstraction allows the RPC server to be decoupled from the node.

### nano::json_handler (nano/node/json_handler.hpp)

The command router and implementation:

- Routes incoming actions to ~100+ handler methods
- Directly accesses node components (ledger, wallets, elections, etc.)
- Implements all RPC commands as member functions
- Returns responses as JSON via callback function
- Location: `nano/node/json_handler.cpp` (~5500 lines)

## Request Flow

### In-Process Flow

```
1. Client sends HTTP POST with JSON body: {"action": "account_balance", "account": "nano_..."}
2. rpc::accept() accepts connection
3. rpc_connection::parse_connection() reads HTTP request
4. rpc_handler validates JSON and extracts action
5. inprocess_rpc_handler::process_request() called
6. json_handler::process_request() routes to handler
7. json_handler::account_balance() executes
8. Response JSON sent back via callback
9. HTTP response written to client
```

### IPC/Daemon Flow

```
1. Client → HTTP POST → nano_rpc daemon
2. nano_rpc: rpc_connection parses request
3. nano_rpc: ipc_rpc_processor queues request
4. nano_rpc: IPC client sends request to node
5. Node: IPC server receives request
6. Node: inprocess_rpc_handler processes
7. Node: json_handler executes command
8. Node → IPC response → nano_rpc
9. nano_rpc → HTTP response → Client
```

## Command Registration

Commands are registered in a static map for efficient routing:

```cpp
// In json_handler.cpp
ipc_json_handler_no_arg_func_map create_ipc_json_handler_no_arg_func_map() {
    ipc_json_handler_no_arg_func_map no_arg_funcs;
    no_arg_funcs.emplace("account_balance", &nano::json_handler::account_balance);
    no_arg_funcs.emplace("account_info", &nano::json_handler::account_info);
    no_arg_funcs.emplace("block_count", &nano::json_handler::block_count);
    // ... 100+ more commands
    return no_arg_funcs;
}
```

Commands not in the map are handled by explicit if/else checks in `json_handler::process_request()`.

## Configuration

RPC configuration is managed via `config-rpc.toml`:

### Key Configuration Options

```toml
# Bind address for the RPC server
address = "::ffff:0.0.0.0"

# Port number
port = 7076  # Default for live network

# Enable control-level commands (dangerous commands)
enable_control = false

# Maximum JSON nesting depth
max_json_depth = 20

# Maximum request size in bytes
max_request_size = 33554432  # 32 MB

# Logging
[rpc_logging]
log_rpc = true  # Log all RPC requests
```

### Configuration Class Hierarchy

- **`nano::rpc_config`** (`nano/lib/rpcconfig.hpp`): Base RPC config
  - `address`, `port`, `enable_control`
  - `max_json_depth`, `max_request_size`
  - `rpc_logging` settings
  - `rpc_process` settings (for daemon mode)

- **`nano::node_rpc_config`** (`nano/node/node_rpc_config.hpp`): Node-specific extensions

## Security Features

### Control Commands

Certain "dangerous" commands require `enable_control = true`:

- Wallet operations (sending, signing, key management)
- Node control (stopping, bootstrap control)
- Database operations
- Some statistics endpoints

Control commands are checked in `rpc_handler.cpp`:

```cpp
std::unordered_set<std::string> rpc_control_impl_set = create_rpc_control_impls();

if (found != rpc_control_impl_set.cend() && !rpc_config.enable_control) {
    json_error_response(response, "RPC control is disabled");
}
```

**Best Practice**: Keep `enable_control = false` in production, enable only when needed with proper access controls.

### Request Limits

- **JSON Depth Limit**: Prevents deeply nested JSON attacks (default: 20 levels)
- **Request Size Limit**: Prevents large payload DoS (default: 32 MB)

### Authentication

The RPC system itself does not implement authentication. In production:

- Use reverse proxy (nginx, caddy) for HTTPS and authentication
- Use firewall rules to restrict access
- Consider VPN or SSH tunneling for remote access
- Only expose RPC on localhost unless properly secured

## RPC Versions

### RPC v1 (Default)

Simple JSON format:

```json
{
  "action": "account_balance",
  "account": "nano_3abc..."
}
```

### RPC v2 (IPC API)

Envelope format with metadata:

```json
{
  "message_type": "account_balance",
  "credentials": "optional_auth_token",
  "correlation_id": "unique_request_id",
  "message": {
    "account": "nano_3abc..."
  }
}
```

RPC v2 is primarily used for IPC communication but can also be accessed via HTTP by setting appropriate headers.

## Error Handling

Errors are returned as JSON:

```json
{
  "error": "Account not found"
}
```

Common error sources:
- JSON parsing failures
- Invalid account/block/hash formats
- Missing required parameters
- Wallet locked
- Block not found in ledger
- RPC control disabled

Error codes are defined in `nano/lib/errors.hpp`.

## Performance Considerations

### Async Processing

Most RPC handlers execute synchronously, but some operations are async:
- **Block processing**: `process` command waits for block validation
- **Wallet operations**: `send`, `receive` generate work asynchronously
- **Work generation**: Can take seconds depending on hardware

### Database Transactions

Handlers create read or write transactions as needed:

```cpp
auto transaction = node.ledger.tx_begin_read();
auto info = node.ledger.any.account_get(transaction, account);
```

Long-running queries should use read transactions to avoid blocking writes.

### Thread Pool

The RPC server uses Boost.Asio thread pool:
- Default: `max(hardware_concurrency(), 4)` threads
- Configurable via `rpc_process.io_threads`
- All handlers execute in this pool

## Command Categories

The ~100+ RPC commands fall into these categories:

### Account Operations
- `account_balance`, `account_info`, `account_history`
- `account_representative`, `account_weight`
- `accounts_balances`, `accounts_frontiers`

### Block Operations
- `block_info`, `block_create`, `block_confirm`
- `blocks`, `blocks_info`, `block_account`
- `process` (submit blocks)

### Wallet Operations (require `enable_control`)
- `wallet_create`, `wallet_destroy`
- `send`, `receive`
- `account_create`, `account_remove`
- `wallet_lock`, `password_enter`

### Network Operations
- `peers`, `telemetry`
- `bootstrap`, `bootstrap_status`
- `representatives`, `representatives_online`

### Node Information
- `block_count`, `version`, `uptime`
- `confirmation_active`, `confirmation_history`
- `active_difficulty`, `available_supply`

### Work Operations
- `work_generate`, `work_validate`
- `work_get`, `work_set`, `work_cancel`

### Development/Debug
- `stats`, `database_txn_tracker`
- `unchecked`, `unopened`
- `ledger` (full ledger dump)

## Adding a New RPC Command

To add a new RPC command:

1. **Add method declaration** in `nano/node/json_handler.hpp`:
   ```cpp
   void my_new_command();
   ```

2. **Implement the method** in `nano/node/json_handler.cpp`:
   ```cpp
   void nano::json_handler::my_new_command() {
       auto account = account_impl();
       if (!ec) {
           // Implementation here
           response_l.put("result", "success");
       }
       response_errors();
   }
   ```

3. **Register in command map** (in `create_ipc_json_handler_no_arg_func_map()`):
   ```cpp
   no_arg_funcs.emplace("my_new_command", &nano::json_handler::my_new_command);
   ```

4. **Add tests** in `nano/rpc_test/`:
   ```cpp
   TEST(rpc, my_new_command) {
       // Test implementation
   }
   ```

5. **Update documentation** (external docs at docs.nano.org)

## Testing

RPC tests are located in `nano/rpc_test/`:

```bash
# Run all RPC tests
./build/rpc_test

# Run specific test
./build/rpc_test --gtest_filter=rpc.account_balance
```

Tests use `nano::rpc_context` helper class to set up test environments.

## Debugging

### Enable RPC Request Logging

In `config-rpc.toml`:
```toml
[rpc_logging]
log_rpc = true
```

This logs every request:
```
[rpc_request] Request abc123 : {"action":"account_balance","account":"nano_..."}
```

### Common Issues

**"Unknown command"**: Command name misspelled or not registered

**"RPC control is disabled"**: Need `enable_control = true` for that command

**"Wallet locked"**: Call `password_enter` first

**Connection refused**: Check `address` and `port` in config, firewall rules

**Slow responses**: Check database transaction contention, enable trace logging

## Integration with Node

The RPC system integrates tightly with the node:

```cpp
class nano::json_handler {
    nano::node & node;  // Direct reference to node

    // Access node components:
    node.ledger          // Blockchain state
    node.wallets         // Wallet management
    node.active          // Active elections
    node.block_processor // Block processing
    node.network         // P2P network
    node.stats           // Statistics
    // ... and 50+ other components
};
```

This direct access means:
- RPC commands reflect real-time node state
- No serialization overhead for in-process mode
- Changes via RPC immediately affect node behavior

## References

- **RPC Documentation**: https://docs.nano.org/commands/rpc-protocol/
- **Integration Guide**: https://docs.nano.org/integration-guides/the-basics/
- **Source Code**:
  - `nano/rpc/` - RPC server implementation
  - `nano/node/json_handler.cpp` - Command implementations
  - `nano/nano_rpc/` - Standalone daemon
  - `nano/rpc_test/` - RPC tests
