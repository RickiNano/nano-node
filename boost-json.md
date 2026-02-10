# Full Migration Plan: boost::property_tree to boost::json

## Overview
Complete migration of the nano-node codebase from `boost::property_tree` to `boost::json` for all JSON operations. This eliminates adapter layers and provides a clean, consistent API.

## Scope Summary
- **Files affected**: ~34 files
- **Total changes**: ~680+ property_tree API calls
- **Estimated effort**: Large refactor across core infrastructure

---

## Migration Order

### Phase 1: Foundation
Set up boost::json dependency and create helper utilities.

| File | Changes | Priority |
|------|---------|----------|
| `CMakeLists.txt` | Add `json` to BOOST_MODULE_LIBS | Required first |
| `nano/lib/json_error_response.hpp` | Simple inline function, 5 lines | Quick win |

### Phase 2: Core Serialization
Migrate block and vote serialization - these are used everywhere.

| File | Occurrences | Notes |
|------|-------------|-------|
| `nano/lib/blocks.hpp` | 22 | Virtual method signatures |
| `nano/lib/blocks.cpp` | 31 | 5 block types × serialize + deserialize |
| `nano/lib/vote.hpp` | 1 | Signature only |
| `nano/lib/vote.cpp` | 5 | Array serialization |

### Phase 3: RPC Handler
The central JSON-RPC handler.

| File | Occurrences | Notes |
|------|-------------|-------|
| `nano/node/json_handler.hpp` | 2 | Member types |
| `nano/node/json_handler.cpp` | 119 | Main handler, all RPC commands |
| `nano/node/node_rpc_config.hpp` | 2 | Callback signature |

### Phase 4: Supporting Infrastructure

| File | Occurrences | Notes |
|------|-------------|-------|
| `nano/node/websocket.hpp` | 7 | Option parsing |
| `nano/node/websocket.cpp` | 26 | Message construction |
| `nano/node/rpc_callbacks.cpp` | 2 | HTTP callback payload |
| `nano/lib/stats_sinks.hpp` | 9 | Stats JSON output |
| `nano/rpc/rpc_handler.hpp` | 1 | Include |
| `nano/rpc/rpc_handler.cpp` | 6 | Handler |

### Phase 5: Test Infrastructure

| File | Occurrences | Notes |
|------|-------------|-------|
| `nano/rpc_test/test_response.hpp` | 4 | Request/response types |
| `nano/rpc_test/test_response.cpp` | 4 | HTTP serialization |
| `nano/rpc_test/rpc_context.hpp` | 3 | Helper signatures |
| `nano/rpc_test/rpc_context.cpp` | 4 | Helper implementations |
| `nano/rpc_test/rpc.cpp` | 271 | **Largest file** - all RPC tests |
| `nano/rpc_test/receivable.cpp` | 28 | Receivable tests |

### Phase 6: Remaining Files

| File | Occurrences | Notes |
|------|-------------|-------|
| `nano/lib/jsonconfig.hpp` | 6 | Config wrapper class |
| `nano/lib/jsonconfig.cpp` | 9 | Config I/O |
| `nano/node/wallet.cpp` | 4 | Wallet serialization |
| `nano/node/distributed_work.cpp` | 6 | Work results |
| `nano/store/backend.cpp` | 2 | Stats |
| `nano/store/txn_tracking.cpp` | 2 | Transaction stats |
| `nano/core_test/block.cpp` | 10 | Block tests |
| `nano/core_test/websocket.cpp` | 40 | WebSocket tests |

---

## API Mapping Reference

### Includes
```cpp
// REMOVE:
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ptree_fwd.hpp>

// ADD:
#include <boost/json.hpp>
```

### Type Changes
```cpp
// OLD → NEW
boost::property_tree::ptree → boost::json::object (for objects)
boost::property_tree::ptree → boost::json::array (for arrays)
boost::property_tree::ptree → boost::json::value (for generic)
```

### Parsing
```cpp
// OLD:
std::stringstream istream(body);
boost::property_tree::ptree request;
boost::property_tree::read_json(istream, request);

// NEW:
auto request = boost::json::parse(body).as_object();
```

### Serialization
```cpp
// OLD:
std::stringstream ostream;
boost::property_tree::write_json(ostream, response_l);
return ostream.str();

// NEW:
return boost::json::serialize(response_l);
```

### Getting Required Values
```cpp
// OLD:
std::string val = request.get<std::string>("key");
bool flag = request.get<bool>("key");
int num = request.get<int>("key");

// NEW:
std::string val = request.at("key").as_string().c_str();
bool flag = request.at("key").as_bool();
int64_t num = request.at("key").as_int64();
```

### Getting Values with Defaults
```cpp
// OLD:
bool flag = request.get<bool>("key", true);

// NEW:
bool flag = true;
if (auto* p = request.if_contains("key"))
    flag = p->as_bool();
```

### Getting Optional Values
```cpp
// OLD:
auto opt = request.get_optional<std::string>("key");
if (opt.is_initialized()) {
    auto val = opt.get();
}

// NEW:
if (auto* p = request.if_contains("key")) {
    auto val = p->as_string();
}
```

### Getting Child Objects/Arrays
```cpp
// OLD:
auto child = request.get_child("accounts");
for (auto& item : child) {
    auto val = item.second.data();
}

// NEW:
auto& child = request.at("accounts").as_array();
for (auto& item : child) {
    auto val = item.as_string();
}
```

### Setting Values
```cpp
// OLD:
response_l.put("key", value);
response_l.put("number", 123);

// NEW:
response_l["key"] = value;
response_l["number"] = 123;
```

### Building Arrays
```cpp
// OLD:
boost::property_tree::ptree accounts;
for (auto& acc : list) {
    boost::property_tree::ptree entry;
    entry.put("", acc.to_account());
    accounts.push_back(std::make_pair("", entry));
}
response_l.add_child("accounts", accounts);

// NEW:
boost::json::array accounts;
for (auto& acc : list) {
    accounts.push_back(acc.to_account());
}
response_l["accounts"] = std::move(accounts);
```

### Building Nested Objects
```cpp
// OLD:
boost::property_tree::ptree entry;
entry.put("balance", balance);
entry.put("pending", pending);
balances.add_child(account_text, entry);

// NEW:
boost::json::object entry;
entry["balance"] = balance;
entry["pending"] = pending;
balances[account_text] = std::move(entry);
```

---

## Block Serialization Changes

### blocks.hpp - Signature Changes
```cpp
// OLD (line 42-43):
virtual void serialize_json(std::string &, bool = false) const = 0;
virtual void serialize_json(boost::property_tree::ptree &) const = 0;

// NEW:
virtual void serialize_json(std::string &, bool = false) const = 0;
virtual void serialize_json(boost::json::object &) const = 0;
```

### blocks.cpp - Implementation Pattern
```cpp
// OLD:
void nano::send_block::serialize_json(boost::property_tree::ptree & tree) const
{
    tree.put("type", "send");
    tree.put("previous", hashables.previous.to_string());
    tree.put("destination", hashables.destination.to_account());
    tree.put("balance", hashables.balance.to_string());
    tree.put("work", nano::to_string_hex(work));
    tree.put("signature", signature.to_string());
}

// NEW:
void nano::send_block::serialize_json(boost::json::object & obj) const
{
    obj["type"] = "send";
    obj["previous"] = hashables.previous.to_string();
    obj["destination"] = hashables.destination.to_account();
    obj["balance"] = hashables.balance.to_string();
    obj["work"] = nano::to_string_hex(work);
    obj["signature"] = signature.to_string();
}
```

### String Serialization (keeps using object internally)
```cpp
void nano::send_block::serialize_json(std::string & string_a, bool single_line) const
{
    boost::json::object obj;
    serialize_json(obj);
    string_a = boost::json::serialize(obj);
}
```

### Block Deserialization
```cpp
// OLD:
std::shared_ptr<nano::block> deserialize_block_json(
    boost::property_tree::ptree const &,
    nano::block_uniquer * = nullptr);

// NEW:
std::shared_ptr<nano::block> deserialize_block_json(
    boost::json::object const &,
    nano::block_uniquer * = nullptr);
```

### Block Constructors
```cpp
// OLD:
nano::send_block::send_block(bool & error_a, boost::property_tree::ptree const & tree_a)

// NEW:
nano::send_block::send_block(bool & error_a, boost::json::object const & obj)
{
    try {
        hashables.previous.decode_hex(obj.at("previous").as_string().c_str());
        hashables.destination.decode_account(obj.at("destination").as_string().c_str());
        // ...
    } catch (...) {
        error_a = true;
    }
}
```

---

## Key Files Detail

### nano/lib/json_error_response.hpp
```cpp
// Complete replacement:
#pragma once
#include <boost/json.hpp>
#include <functional>
#include <string>

namespace nano
{
inline void json_error_response(
    std::function<void(std::string const &)> response_a,
    std::string const & message_a)
{
    boost::json::object response;
    response["error"] = message_a;
    response_a(boost::json::serialize(response));
}
}
```

### nano/node/json_handler.hpp - Member Changes
```cpp
// Line 155, 160 - Change:
boost::json::object request;
boost::json::object response_l;
```

### nano/node/node_rpc_config.hpp - Callback Signature
```cpp
// OLD:
std::function<void(boost::property_tree::ptree const &)> request_callback;

// NEW:
std::function<void(boost::json::object const &)> request_callback;
```

---

## Implementation Strategy

### Step-by-Step for Each File

1. **Update includes** - Remove property_tree, add boost/json.hpp
2. **Change types** - ptree → object/array/value
3. **Update API calls** - Use mapping reference above
4. **Compile and fix** - Let compiler guide remaining issues
5. **Run tests** - Verify functionality

### Handling Complex Cases

**Nested iteration:**
```cpp
// OLD:
for (auto& item : request.get_child("accounts")) {
    auto account_text = item.second.data();
}

// NEW:
for (auto& item : request.at("accounts").as_array()) {
    auto account_text = item.as_string();
}
```

**Count check:**
```cpp
// OLD:
if (request.count("field") > 0)

// NEW:
if (request.contains("field"))
```

---

## Verification

### Build
```bash
cmake --build build --target nano_node nano_rpc
```

### Unit Tests
```bash
ctest -R core_test --output-on-failure
ctest -R rpc_test --output-on-failure
```

### Manual RPC Tests
```bash
curl -d '{"action":"version"}' http://localhost:7076
curl -d '{"action":"block_count"}' http://localhost:7076
curl -d '{"action":"account_balance","account":"nano_..."}' http://localhost:7076
```

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| JSON parsing behavior differs | boost::json is stricter; add try-catch where needed |
| Number handling differences | boost::json uses int64/uint64/double explicitly |
| Empty string handling | Test edge cases with empty values |
| Large test file (rpc.cpp) | Can split into multiple PRs if needed |

---

## Estimated File Change Counts

| Phase | Files | Est. Line Changes |
|-------|-------|-------------------|
| Phase 1: Foundation | 2 | ~20 |
| Phase 2: Core Serialization | 4 | ~400 |
| Phase 3: RPC Handler | 3 | ~500 |
| Phase 4: Infrastructure | 6 | ~200 |
| Phase 5: Tests | 6 | ~800 |
| Phase 6: Remaining | 10 | ~200 |
| **Total** | **~31** | **~2100** |
