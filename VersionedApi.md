# Versioned RPC API Implementation Plan for nano-node

## Overview

Implement a new versioned RPC API (v3+) with modern JSON handling using Boost.JSON, while keeping existing handler architecture. Use URL path versioning (/api/v3, /api/v4) with a clean break from v1/v2.

**User Requirements:**
- ✅ URL path versioning (/api/v3, /api/v4)
- ✅ Clean break from v1/v2 (focus on new API)
- ✅ Use Boost.JSON (already in submodules)
- ✅ Keep existing handler architecture (JSON + Versioning only)

## Quick Summary

**Current State:**
- 136 RPC endpoints using Boost.PropertyTree (slow)
- v1: POST / with action-based JSON
- v2: POST /api/v2 with Flatbuffers
- Request flow: rpc_connection → rpc_handler → json_handler

**Proposed Changes:**
- Add v3+ using Boost.JSON (5-10x faster)
- URL routing: /api/v3 for new endpoints
- Version registry pattern for extensibility
- Keep v1 working (no breaking changes)
- Reuse existing handler logic where possible

## Phase 1: Foundation (Week 1-2, ~50 hours)

### Goals
Set up Boost.JSON and version routing infrastructure

### Tasks

#### 1.1 Enable Boost.JSON in Build System

**File: nano/lib/CMakeLists.txt (line ~151)**
```cmake
target_link_libraries(nano_lib
  ...
  Boost::iostreams
  Boost::json        # ADD THIS LINE
  Boost::log
  ...
)
```

**File: nano/rpc/CMakeLists.txt (line ~12)**
```cmake
target_link_libraries(rpc
  nano_lib
  Boost::json        # ADD THIS LINE
)
```

**File: submodules/boost/CMakeLists.txt**
- Add `add_subdirectory(libs/json)` to build Boost.JSON

#### 1.2 Create Version Handler Interface

**New file: nano/rpc/rpc_version_handler.hpp**
```cpp
#pragma once
#include <functional>
#include <memory>
#include <string>

namespace nano
{
class node;
class node_rpc_config;

class rpc_version_handler
{
public:
    virtual ~rpc_version_handler () = default;
    virtual void process_request (
        std::string const & body,
        std::function<void (std::string const &)> response) = 0;
    virtual int version () const = 0;
};
}
```

#### 1.3 Create Version Registry

**New file: nano/rpc/rpc_version_registry.hpp**
```cpp
#pragma once
#include <nano/rpc/rpc_version_handler.hpp>
#include <unordered_map>
#include <vector>

namespace nano
{
class rpc_version_registry
{
public:
    rpc_version_registry (nano::node &, nano::node_rpc_config const &);

    void register_handler (int version, std::shared_ptr<rpc_version_handler> handler);
    std::shared_ptr<rpc_version_handler> get_handler (int version);
    std::vector<int> supported_versions () const;
    bool is_supported (int version) const;

private:
    nano::node & node;
    nano::node_rpc_config const & config;
    std::unordered_map<int, std::shared_ptr<rpc_version_handler>> handlers;
};
}
```

**New file: nano/rpc/rpc_version_registry.cpp**
- Implement registry methods
- Auto-register v3 handler on construction

#### 1.4 Update Version Detection

**File: nano/rpc/rpc_connection.cpp (line ~130)**

Current:
```cpp
std::string api_path_l = "/api/v2";
int rpc_version_l = boost::starts_with (path_l, api_path_l) ? 2 : 1;
```

Change to:
```cpp
int rpc_version_l = 1; // default
if (boost::starts_with (path_l, "/api/v"))
{
    // Extract version number (e.g., "/api/v3" → 3)
    if (path_l.length () >= 7)
    {
        char version_char = path_l[6];
        if (version_char >= '0' && version_char <= '9')
        {
            rpc_version_l = version_char - '0';
        }
    }
}
```

#### 1.5 Update RPC Handler Dispatch

**File: nano/rpc/rpc_handler.cpp (add version registry)**

Add member:
```cpp
std::shared_ptr<nano::rpc_version_registry> version_registry;
```

In `process_request()`, add v3+ routing:
```cpp
if (rpc_version_l >= 3)
{
    auto handler = version_registry->get_handler (rpc_version_l);
    if (handler)
    {
        handler->process_request (body_l, response_handler);
    }
    else
    {
        // Return error: unsupported version
    }
}
else
{
    // Existing v1/v2 logic
}
```

#### 1.6 Create V3 Response Builder

**New file: nano/rpc/v3/response_builder.hpp**
```cpp
#pragma once
#include <boost/json.hpp>
#include <string>

namespace nano::rpc::v3
{
class response_builder
{
public:
    // Success response
    static boost::json::object success (boost::json::value data);

    // Error response
    static boost::json::object error (std::string const & code,
        std::string const & message,
        boost::json::object details = {});

    // Serialize to string
    static std::string serialize (boost::json::object const & obj);
};
}
```

#### 1.7 Define Error Codes

**New file: nano/rpc/v3/error_codes.hpp**
```cpp
#pragma once
#include <string_view>

namespace nano::rpc::v3::errors
{
// Request errors (1000-1099)
constexpr std::string_view INVALID_JSON = "INVALID_JSON";
constexpr std::string_view MISSING_REQUIRED_FIELD = "MISSING_REQUIRED_FIELD";
constexpr std::string_view UNKNOWN_ACTION = "UNKNOWN_ACTION";

// Account errors (1100-1199)
constexpr std::string_view ACCOUNT_NOT_FOUND = "ACCOUNT_NOT_FOUND";
constexpr std::string_view INVALID_ACCOUNT_FORMAT = "INVALID_ACCOUNT_FORMAT";

// Block errors (1200-1299)
constexpr std::string_view BLOCK_NOT_FOUND = "BLOCK_NOT_FOUND";
constexpr std::string_view INVALID_BLOCK_HASH = "INVALID_BLOCK_HASH";

// Add more as needed...
}
```

**Deliverables:**
- [ ] CMake changes for Boost.JSON
- [ ] Version handler interface
- [ ] Version registry implementation
- [ ] Updated rpc_connection.cpp
- [ ] Updated rpc_handler.cpp
- [ ] Response builder utility
- [ ] Error code definitions
- [ ] Unit tests for version routing

## Phase 2: V3 Handler Skeleton (Week 3, ~20 hours)

### Goals
Create v3 handler structure with action dispatch

### Tasks

#### 2.1 Create V3 Handler Class

**New file: nano/rpc/v3/rpc_v3_handler.hpp**
```cpp
#pragma once
#include <nano/rpc/rpc_version_handler.hpp>
#include <boost/json.hpp>
#include <unordered_map>
#include <functional>

namespace nano
{
class rpc_v3_handler : public rpc_version_handler
{
public:
    rpc_v3_handler (nano::node &, nano::node_rpc_config const &);

    void process_request (
        std::string const & body,
        std::function<void (std::string const &)> response) override;

    int version () const override { return 3; }

private:
    nano::node & node;
    nano::node_rpc_config const & config;

    // Action dispatch map
    using handler_func = std::function<boost::json::object (boost::json::object const &)>;
    std::unordered_map<std::string, handler_func> action_handlers;

    // Register all handlers
    void register_handlers ();

    // Handler methods (to be implemented in phases)
    boost::json::object handle_version (boost::json::object const & request);
    boost::json::object handle_account_balance (boost::json::object const & request);
    // ... more handlers
};
}
```

**New file: nano/rpc/v3/rpc_v3_handler.cpp**
```cpp
#include <nano/rpc/v3/rpc_v3_handler.hpp>
#include <nano/rpc/v3/response_builder.hpp>
#include <nano/rpc/v3/error_codes.hpp>
#include <nano/node/node.hpp>

namespace nano
{

rpc_v3_handler::rpc_v3_handler (nano::node & node_a, nano::node_rpc_config const & config_a) :
    node (node_a),
    config (config_a)
{
    register_handlers ();
}

void rpc_v3_handler::register_handlers ()
{
    action_handlers["version"] = [this](auto const & req) {
        return handle_version (req);
    };
    action_handlers["account_balance"] = [this](auto const & req) {
        return handle_account_balance (req);
    };
    // Register more as they're implemented
}

void rpc_v3_handler::process_request (
    std::string const & body,
    std::function<void (std::string const &)> response)
{
    try
    {
        // Parse JSON
        boost::json::value request_value = boost::json::parse (body);
        auto & request_obj = request_value.as_object ();

        // Extract action
        if (!request_obj.contains ("action"))
        {
            auto error_response = rpc::v3::response_builder::error (
                std::string (rpc::v3::errors::MISSING_REQUIRED_FIELD),
                "Missing required field: action"
            );
            response (rpc::v3::response_builder::serialize (error_response));
            return;
        }

        std::string action = request_obj.at ("action").as_string ().c_str ();

        // Dispatch to handler
        auto handler_it = action_handlers.find (action);
        if (handler_it == action_handlers.end ())
        {
            auto error_response = rpc::v3::response_builder::error (
                std::string (rpc::v3::errors::UNKNOWN_ACTION),
                "Unknown action: " + action
            );
            response (rpc::v3::response_builder::serialize (error_response));
            return;
        }

        // Call handler
        auto result = handler_it->second (request_obj);
        response (rpc::v3::response_builder::serialize (result));
    }
    catch (std::exception const & e)
    {
        auto error_response = rpc::v3::response_builder::error (
            std::string (rpc::v3::errors::INVALID_JSON),
            std::string ("JSON parsing error: ") + e.what ()
        );
        response (rpc::v3::response_builder::serialize (error_response));
    }
}

} // namespace nano
```

**Deliverables:**
- [ ] V3 handler class skeleton
- [ ] Action dispatch mechanism
- [ ] JSON parsing with error handling
- [ ] Register handler in version registry
- [ ] Integration tests

## Phase 3: Core Endpoints (Week 4-6, ~120 hours)

### Goals
Implement 20 most critical read-only endpoints

### Priority Endpoints

**Tier 1 - Info/Query (10 endpoints):**
1. version
2. account_balance
3. account_info
4. block_info
5. block_count
6. available_supply
7. peers
8. uptime
9. validate_account_number
10. account_representative

**Tier 2 - Lists/Bulk (10 endpoints):**
11. accounts_balances
12. blocks_info
13. account_history
14. representatives
15. representatives_online
16. frontiers
17. ledger
18. delegators
19. account_block_count
20. block_account

### Implementation Pattern

For each endpoint, follow this pattern:

**Example: account_balance**

```cpp
boost::json::object rpc_v3_handler::handle_account_balance (boost::json::object const & request)
{
    // 1. Extract and validate parameters
    if (!request.contains ("account"))
    {
        return rpc::v3::response_builder::error (
            std::string (rpc::v3::errors::MISSING_REQUIRED_FIELD),
            "Missing required field: account"
        );
    }

    std::string account_text = request.at ("account").as_string ().c_str ();
    nano::account account;

    if (account.decode_account (account_text))
    {
        return rpc::v3::response_builder::error (
            std::string (rpc::v3::errors::INVALID_ACCOUNT_FORMAT),
            "Invalid account format"
        );
    }

    // 2. Execute business logic (reuse existing node methods)
    bool include_only_confirmed = request.contains ("include_only_confirmed")
        ? request.at ("include_only_confirmed").as_bool ()
        : true;

    auto balance_pending = node.balance_pending (account, include_only_confirmed);

    // 3. Build response
    boost::json::object data;
    data["balance"] = balance_pending.first.convert_to<std::string> ();
    data["pending"] = balance_pending.second.convert_to<std::string> ();
    data["receivable"] = balance_pending.second.convert_to<std::string> ();

    return rpc::v3::response_builder::success (data);
}
```

### File Organization

**New files:**
- nano/rpc/handlers/v3/account_handlers.cpp (~800 lines)
- nano/rpc/handlers/v3/block_handlers.cpp (~500 lines)
- nano/rpc/handlers/v3/node_handlers.cpp (~300 lines)
- nano/rpc/handlers/v3/utility_handlers.cpp (~200 lines)

**Test files:**
- nano/rpc_test/v3/test_account_handlers.cpp
- nano/rpc_test/v3/test_block_handlers.cpp
- nano/rpc_test/v3/test_node_handlers.cpp

**Deliverables:**
- [ ] 20 core endpoints implemented
- [ ] Unit tests for each endpoint
- [ ] Integration tests
- [ ] Comparison tests (v1 vs v3 output)

## Phase 4: Remaining Read-Only Endpoints (Week 7-9, ~200 hours)

### Goals
Complete all read-only endpoints (~96 remaining)

### Categories
- Account operations (16 more)
- Block operations (8 more)
- Wallet read operations (10)
- Work query operations (3)
- Network/bootstrap status (8)
- Ledger queries (10)
- Utility/conversion (8)

### Implementation Strategy
- Group similar endpoints together
- Reuse validation logic
- Create helper functions for common patterns
- Focus on correctness over optimization

**Deliverables:**
- [ ] All read-only endpoints implemented
- [ ] Full test coverage
- [ ] Performance benchmarks (v3 vs v1)

## Phase 5: State-Modifying Endpoints (Week 10-12, ~240 hours)

### Goals
Implement endpoints that modify state (~30 endpoints)

### Critical Endpoints
- send
- receive
- block_create
- block_confirm
- process
- account_create
- wallet_create
- wallet_add
- account_representative_set
- wallet_representative_set
- work_generate

### Special Considerations
- Async operations (worker threads)
- Callback handling
- Work generation
- Wallet locking/unlocking
- Transaction safety

**Deliverables:**
- [ ] State-modifying endpoints implemented
- [ ] Async operation support
- [ ] Extensive testing (functional + integration)
- [ ] Safety validation (permissions, locks, etc.)

## Phase 6: Control-Level Endpoints (Week 13-14, ~200 hours)

### Goals
Implement control-level admin endpoints (~16 endpoints)

### Endpoints
- stop
- keepalive
- bootstrap_* (6 endpoints)
- work_* control operations
- database_txn_tracker
- epoch_upgrade
- node_id_delete
- unchecked_clear
- Others requiring enable_control=true

### Special Considerations
- Permission validation
- Dangerous operations (requires enable_control config)
- Proper error messages for permission denied

**Deliverables:**
- [ ] Control-level endpoints implemented
- [ ] Permission checks enforced
- [ ] Admin operation logging
- [ ] Security audit

## Phase 7: Testing & Polish (Week 15-16, ~80 hours)

### Goals
Final testing, documentation, and polish

### Tasks
- [ ] Comprehensive integration test suite
- [ ] Performance benchmarking (v1 vs v3)
- [ ] Load testing
- [ ] Error case testing
- [ ] Documentation
- [ ] Migration guide
- [ ] API reference
- [ ] Example code (curl, Python, JavaScript)
- [ ] Changelog

## Critical Files Reference

### Files to Modify (5 files)

1. **nano/rpc/rpc_connection.cpp** (line ~130)
   - Update version detection for v3+

2. **nano/rpc/rpc_handler.cpp** (line ~30-139)
   - Add version registry dispatch

3. **nano/lib/CMakeLists.txt** (line ~151)
   - Add Boost::json dependency

4. **nano/rpc/CMakeLists.txt** (line ~12)
   - Add Boost::json and v3 source files

5. **submodules/boost/CMakeLists.txt**
   - Enable Boost.JSON compilation

### New Files to Create (~20+ files)

**Infrastructure:**
- nano/rpc/rpc_version_handler.hpp
- nano/rpc/rpc_version_registry.hpp
- nano/rpc/rpc_version_registry.cpp
- nano/rpc/v3/response_builder.hpp
- nano/rpc/v3/response_builder.cpp
- nano/rpc/v3/error_codes.hpp
- nano/rpc/v3/error_codes.cpp

**V3 Handler:**
- nano/rpc/v3/rpc_v3_handler.hpp
- nano/rpc/v3/rpc_v3_handler.cpp

**Endpoint Handlers:**
- nano/rpc/handlers/v3/account_handlers.cpp
- nano/rpc/handlers/v3/block_handlers.cpp
- nano/rpc/handlers/v3/wallet_handlers.cpp
- nano/rpc/handlers/v3/work_handlers.cpp
- nano/rpc/handlers/v3/node_handlers.cpp
- nano/rpc/handlers/v3/ledger_handlers.cpp
- nano/rpc/handlers/v3/bootstrap_handlers.cpp
- nano/rpc/handlers/v3/utility_handlers.cpp

**Tests:**
- nano/rpc_test/v3/test_*.cpp (8+ test files)

## Timeline & Estimates

**Full Implementation:**
- Phase 1 (Foundation): 2 weeks, ~50 hours
- Phase 2 (Skeleton): 1 week, ~20 hours
- Phase 3 (Core Endpoints): 3 weeks, ~120 hours
- Phase 4 (Read-Only): 3 weeks, ~200 hours
- Phase 5 (State-Modifying): 3 weeks, ~240 hours
- Phase 6 (Control-Level): 2 weeks, ~200 hours
- Phase 7 (Polish): 2 weeks, ~80 hours

**Total: 16 weeks, ~910 hours (1 FTE)**

**Minimum Viable Product (MVP):**
- Phase 1-3 only: 6 weeks, ~190 hours
- Delivers: v3 infrastructure + 20 core endpoints

## Success Criteria

### Functional
- [ ] All 136 endpoints available in v3
- [ ] v1 continues working unchanged
- [ ] v3 responses conform to schema
- [ ] Proper error codes for all cases
- [ ] 100% test coverage

### Performance
- [ ] v3 JSON parsing ≥20% faster than v1
- [ ] v3 serialization ≥15% faster than v1
- [ ] No memory leaks

### Quality
- [ ] Zero crashes in production testing
- [ ] All inputs validated
- [ ] Helpful error messages
- [ ] Code review approved
- [ ] Documentation complete

## Response Format Examples

### Success Response
```json
{
  "success": true,
  "data": {
    "balance": "10000000000000000000000000000000",
    "pending": "0",
    "receivable": "0"
  },
  "error": null,
  "version": 3
}
```

### Error Response
```json
{
  "success": false,
  "data": null,
  "error": {
    "code": "ACCOUNT_NOT_FOUND",
    "message": "Account not found in ledger",
    "details": {
      "account": "nano_1invalid..."
    }
  },
  "version": 3
}
```

## Migration Notes

### For API Consumers
1. Update endpoint URLs: `/` → `/api/v3`
2. Parse new response format (success/data/error envelope)
3. Handle new error codes
4. Update to Boost.JSON-compatible data types

### For Developers
1. Use Boost.JSON instead of property_tree for new code
2. Follow v3 handler pattern for new endpoints
3. Register new handlers in rpc_v3_handler::register_handlers()
4. Add comprehensive tests for each endpoint

## Next Steps

1. **Review and approve this plan**
2. **Set up development branch** (e.g., `feature/rpc-v3`)
3. **Start Phase 1** (Foundation)
4. **Create tracking issues** for each phase
5. **Regular progress reviews** (weekly recommended)

---

**Plan created:** 2025-12-03
**Target start:** After approval
**Estimated completion:** 16 weeks from start (MVP: 6 weeks)
