# RPC Migration Plan: Old API → New Boost.JSON API

## Overview

**Total Endpoints**: 118
**Already Migrated**: 13
**Remaining**: 105

**Migration Status**: ~11% Complete

---

## Already Implemented (13 endpoints)

✅ **Read-Only Info/Query (10)**
1. version
2. account_balance
3. account_info
4. account_representative
5. block_info
6. block_count
7. available_supply
8. peers
9. uptime
10. validate_account_number

✅ **Bulk Operations (2)**
11. accounts_balances
12. blocks_info

✅ **State-Modifying (1)**
13. block_create

---

## Migration Strategy

### Phase 1: Core Read-Only Endpoints (Priority: HIGH)
**Estimated Time**: 20-30 hours
**Endpoints**: 35

These are frequently used, read-only endpoints with simple logic.

#### Account Query Operations (11 endpoints)
- [ ] account_block_count - Query block count for account
- [ ] account_get - Get account from public key
- [ ] account_key - Get public key from account
- [ ] account_list - List all accounts in wallet
- [ ] account_weight - Get voting weight of account
- [ ] account_count - Total number of accounts
- [ ] account_history - Get account transaction history
- [ ] accounts_frontiers - Get frontiers for multiple accounts
- [ ] accounts_representatives - Get representatives for accounts
- [ ] accounts_pending - Get pending for multiple accounts (legacy)
- [ ] accounts_receivable - Get receivable for multiple accounts

#### Block Query Operations (7 endpoints)
- [ ] block_account - Get account from block
- [ ] block_hash - Calculate hash from block JSON
- [ ] blocks - Get multiple blocks by hash
- [ ] chain - Get block chain (successors/predecessors)
- [ ] pruned_exists - Check if block is pruned
- [ ] unchecked - Get unchecked blocks
- [ ] unchecked_get - Get specific unchecked block
- [ ] unchecked_keys - Get unchecked block hashes

#### Network/Node Info (6 endpoints)
- [ ] node_id - Get node ID
- [ ] active_difficulty - Get current PoW difficulty
- [ ] confirmation_quorum - Get confirmation quorum info
- [ ] telemetry - Get telemetry data from peers
- [ ] representatives - List online representatives
- [ ] representatives_online - Get online representatives with weight

#### Ledger Query (5 endpoints)
- [ ] frontiers - Get account frontiers
- [ ] ledger - Query ledger with filters
- [ ] delegators - Get delegators for representative
- [ ] delegators_count - Count delegators
- [ ] unopened - Get unopened accounts with pending

#### Pending/Receivable (4 endpoints)
- [ ] pending - Get pending blocks for account (legacy)
- [ ] receivable - Get receivable blocks for account
- [ ] pending_exists - Check if pending exists
- [ ] receivable_exists - Check if receivable exists

#### Utility/Conversion (2 endpoints)
- [ ] nano_to_raw - Convert Nano to raw
- [ ] raw_to_nano - Convert raw to Nano

---

### Phase 2: Wallet Read-Only Operations (Priority: MEDIUM)
**Estimated Time**: 15-20 hours
**Endpoints**: 14

Wallet query operations without state changes.

- [ ] wallet_info - Get wallet information
- [ ] wallet_balances - Get balances for all accounts in wallet
- [ ] wallet_contains - Check if account is in wallet
- [ ] wallet_frontiers - Get frontiers for wallet accounts
- [ ] wallet_history - Get transaction history for wallet
- [ ] wallet_key_valid - Check if wallet is unlocked
- [ ] wallet_ledger - Get ledger entries for wallet
- [ ] wallet_pending - Get pending for wallet (legacy)
- [ ] wallet_receivable - Get receivable for wallet
- [ ] wallet_representative - Get wallet representative
- [ ] wallet_seed - Get wallet seed (requires unlock)
- [ ] wallet_export - Export wallet
- [ ] wallet_work_get - Get precomputed work for account
- [ ] password_valid - Check if wallet password is valid

---

### Phase 3: Confirmation & Election Info (Priority: MEDIUM)
**Estimated Time**: 10-15 hours
**Endpoints**: 8

Election and confirmation tracking endpoints.

- [ ] confirmation_active - Get active elections
- [ ] confirmation_history - Get confirmation history
- [ ] confirmation_info - Get detailed confirmation info
- [ ] election_statistics - Get election statistics
- [ ] block_confirm - Request block confirmation
- [ ] republish - Republish blocks
- [ ] wallet_republish - Republish wallet blocks

---

### Phase 4: State-Modifying Operations (Priority: HIGH)
**Estimated Time**: 25-35 hours
**Endpoints**: 18

Critical endpoints that modify node/wallet state.

#### Wallet Account Management (6 endpoints)
- [ ] account_create - Create new account in wallet
- [ ] accounts_create - Create multiple accounts
- [ ] account_remove - Remove account from wallet
- [ ] wallet_add - Add private key to wallet
- [ ] wallet_add_watch - Add watch-only account
- [ ] wallet_create - Create new wallet

#### Transaction Operations (7 endpoints)
- [ ] send - Send transaction
- [ ] receive - Receive pending block
- [ ] account_representative_set - Change account representative
- [ ] wallet_representative_set - Set wallet representative
- [ ] account_move - Move account between wallets
- [ ] process - Process block
- [ ] sign - Sign block/hash

#### Wallet Management (5 endpoints)
- [ ] wallet_change_seed - Change wallet seed
- [ ] wallet_destroy - Destroy wallet
- [ ] wallet_lock - Lock wallet
- [ ] password_change - Change wallet password
- [ ] password_enter - Unlock wallet

---

### Phase 5: Work Generation & Validation (Priority: MEDIUM)
**Estimated Time**: 8-12 hours
**Endpoints**: 9

Work/PoW related endpoints.

- [ ] work_generate - Generate work for block
- [ ] work_cancel - Cancel work generation
- [ ] work_get - Get precomputed work
- [ ] work_set - Set precomputed work
- [ ] work_validate - Validate work value
- [ ] work_peer_add - Add work peer
- [ ] work_peers - List work peers
- [ ] work_peers_clear - Clear work peers
- [ ] receive_minimum - Get minimum receive amount
- [ ] receive_minimum_set - Set minimum receive amount

---

### Phase 6: Advanced/Utility Operations (Priority: LOW)
**Estimated Time**: 10-15 hours
**Endpoints**: 11

Less commonly used utility endpoints.

- [ ] key_create - Generate keypair
- [ ] key_expand - Expand private key to keypair
- [ ] deterministic_key - Generate deterministic keypair
- [ ] search_pending - Search for pending (legacy)
- [ ] search_receivable - Search for receivable
- [ ] search_pending_all - Search all pending
- [ ] search_receivable_all - Search all receivable
- [ ] keepalive - Keepalive ping
- [ ] stats - Get node statistics
- [ ] stats_clear - Clear statistics
- [ ] database_txn_tracker - Track database transactions

---

### Phase 7: Bootstrap Operations (Priority: LOW)
**Estimated Time**: 8-12 hours
**Endpoints**: 6

Bootstrap and sync related endpoints.

- [ ] bootstrap - Start legacy bootstrap
- [ ] bootstrap_any - Bootstrap from any peer
- [ ] bootstrap_lazy - Lazy bootstrap
- [ ] bootstrap_status - Get bootstrap status
- [ ] bootstrap_priorities - Get bootstrap priorities
- [ ] bootstrap_reset - Reset bootstrap attempts

---

### Phase 8: Control-Level Operations (Priority: CRITICAL)
**Estimated Time**: 6-10 hours
**Endpoints**: 4

Admin operations requiring `enable_control` permission.

- [ ] stop - Stop the node
- [ ] unchecked_clear - Clear unchecked blocks
- [ ] node_id_delete - Delete node ID
- [ ] epoch_upgrade - Trigger epoch upgrade

---

## Implementation Guidelines

### Standard Pattern for Each Endpoint

```cpp
boost::json::object rpc_v3_handler::handle_ENDPOINT_NAME(boost::json::object const & request) {
    // 1. VALIDATE & EXTRACT PARAMETERS
    if (!request.contains("required_field")) {
        return response_builder::error("Missing required field");
    }

    // Extract and validate each parameter
    // Use appropriate error messages matching old API

    // 2. EXECUTE BUSINESS LOGIC
    // Copy logic from old API json_handler method
    // Use node methods directly (same as old API)

    // 3. BUILD RESPONSE
    boost::json::object data;
    data["field1"] = value1;
    data["field2"] = value2;
    return response_builder::success(data);
}
```

### Error Message Mapping

Match old API error messages exactly:
- Invalid account → `"Bad account number"`
- Missing field → `"Missing required field: FIELD_NAME"` or old API equivalent
- Invalid wallet → `"Wallet not found"`
- Invalid block → `"Block not found"`
- Permission denied → `"RPC control is disabled"`

### Testing Each Endpoint

For each migrated endpoint:
1. Test with same input as old API
2. Verify output matches old API exactly
3. Test error cases (missing fields, invalid data)
4. Compare performance (optional but recommended)

---

## Timeline Estimates

### Aggressive Timeline (Full-time focus)
- **Phase 1**: 3-4 weeks
- **Phase 2**: 2-3 weeks
- **Phase 3**: 1-2 weeks
- **Phase 4**: 3-4 weeks
- **Phase 5**: 1-2 weeks
- **Phase 6**: 1-2 weeks
- **Phase 7**: 1-2 weeks
- **Phase 8**: 1 week

**Total: 13-20 weeks (3-5 months)**

### Moderate Timeline (Part-time or mixed priorities)
**Total: 6-9 months**

### MVP Approach (Prioritize most-used endpoints)
Focus on Phases 1, 3, 4, and 8:
**Total: 8-12 weeks for core functionality**

---

## Priority Matrix

### Must-Have (MVP)
- Phase 1: Core Read-Only (35 endpoints)
- Phase 4: State-Modifying (18 endpoints)
- Phase 8: Control-Level (4 endpoints)

**Total MVP**: 57 endpoints (~60% coverage, ~90% usage coverage)

### Should-Have
- Phase 2: Wallet Read-Only (14 endpoints)
- Phase 3: Confirmation Info (8 endpoints)
- Phase 5: Work Operations (9 endpoints)

### Nice-to-Have
- Phase 6: Advanced Utility (11 endpoints)
- Phase 7: Bootstrap (6 endpoints)

---

## File Organization

Create separate handler files for organization:

```
nano/rpc/v3/handlers/
├── account_handlers.cpp      # Phase 1 account operations
├── block_handlers.cpp         # Phase 1 block operations
├── wallet_handlers.cpp        # Phase 2 wallet operations
├── confirmation_handlers.cpp  # Phase 3 confirmation ops
├── transaction_handlers.cpp   # Phase 4 transactions
├── work_handlers.cpp          # Phase 5 work operations
├── utility_handlers.cpp       # Phase 6 utilities
├── bootstrap_handlers.cpp     # Phase 7 bootstrap
└── control_handlers.cpp       # Phase 8 control ops
```

Each file should be ~200-500 lines and contain related endpoints.

---

## Next Steps

1. **Review and approve this plan**
2. **Choose approach**: MVP, Aggressive, or Moderate timeline
3. **Set up file structure** for handler organization
4. **Start with Phase 1** - Highest ROI, builds momentum
5. **Implement in batches** - 5-10 endpoints at a time
6. **Test continuously** - Compare with old API after each batch

---

**Plan Created**: 2025-12-05
**Status**: Ready for review
**Estimated Total Effort**: 95-130 hours (aggressive) or 190-260 hours (with extensive testing)
