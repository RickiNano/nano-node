# V3 RPC API - Missing Endpoints Analysis

## Summary
- **V1 Endpoints:** 118 total
- **V3 Endpoints:** 37 total (including representatives_online)
- **Missing:** ~81 endpoints

## Currently Implemented in V3 (37 endpoints)

### Info/Query (22)
1. ✅ version
2. ✅ account_balance
3. ✅ account_info
4. ✅ account_block_count
5. ✅ account_weight
6. ✅ account_representative
7. ✅ account_get
8. ✅ account_key
9. ✅ account_count
10. ✅ block_info
11. ✅ block_account
12. ✅ block_count
13. ✅ block_hash
14. ✅ available_supply
15. ✅ peers
16. ✅ uptime
17. ✅ validate_account_number
18. ✅ node_id
19. ✅ active_difficulty
20. ✅ delegators_count
21. ✅ pruned_exists
22. ✅ receivable_exists
23. ✅ receivable
24. ✅ pending (deprecated alias)
25. ✅ pending_exists (deprecated alias)

### Lists/Bulk (4)
26. ✅ accounts_balances
27. ✅ blocks_info
28. ✅ blocks
29. ✅ chain
30. ✅ representatives
31. ✅ representatives_online

### Utility/Conversion (4)
32. ✅ nano_to_raw
33. ✅ raw_to_nano
34. ✅ key_create
35. ✅ key_expand

### State-Modifying (1)
36. ✅ block_create

### Control (1)
37. ✅ stop

---

## MISSING ENDPOINTS (~81)

### HIGH PRIORITY - Read-Only Info/Query (15)

#### Account Operations
- ❌ **account_history** - Get account transaction history (CRITICAL)
- ❌ **account_list** - List accounts in wallet
- ❌ **accounts_frontiers** - Get frontiers for multiple accounts
- ❌ **accounts_representatives** - Get representatives for multiple accounts
- ❌ **accounts_pending** - Get pending blocks for multiple accounts (deprecated)
- ❌ **accounts_receivable** - Get receivable blocks for multiple accounts

#### Ledger/Chain Operations
- ❌ **frontiers** - Get account frontiers (CRITICAL)
- ❌ **frontier_count** - Count frontiers (alias for account_count)
- ❌ **ledger** - Get ledger entries (CRITICAL)
- ❌ **delegators** - Get accounts delegating to a representative (CRITICAL)
- ❌ **unopened** - Get unopened accounts

#### Consensus/Confirmation Operations
- ❌ **confirmation_active** - Get active confirmations (CRITICAL)
- ❌ **confirmation_history** - Get confirmation history (CRITICAL)
- ❌ **confirmation_info** - Get confirmation info for a block (CRITICAL)
- ❌ **confirmation_quorum** - Get confirmation quorum info (CRITICAL)

#### Network/Telemetry
- ❌ **telemetry** - Get telemetry data from peers (IMPORTANT)
- ❌ **stats** - Get node statistics (IMPORTANT)
- ❌ **election_statistics** - Get election statistics

#### Unchecked Blocks
- ❌ **unchecked** - Get unchecked blocks (IMPORTANT)
- ❌ **unchecked_get** - Get specific unchecked block (IMPORTANT)
- ❌ **unchecked_keys** - Get unchecked block keys

#### Deterministic Keys
- ❌ **deterministic_key** - Generate deterministic key from seed

### MEDIUM PRIORITY - State-Modifying Operations (40)

#### Transaction Operations
- ❌ **send** - Send transaction (CRITICAL)
- ❌ **receive** - Receive transaction (CRITICAL)
- ❌ **process** - Process a block (CRITICAL)
- ❌ **sign** - Sign a block (IMPORTANT)
- ❌ **block_confirm** - Request confirmation for a block

#### Account Operations
- ❌ **account_create** - Create account in wallet
- ❌ **account_representative_set** - Set account representative
- ❌ **account_move** - Move account between wallets
- ❌ **account_remove** - Remove account from wallet
- ❌ **accounts_create** - Create multiple accounts

#### Wallet Operations (23 endpoints)
- ❌ **wallet_create** - Create wallet (IMPORTANT)
- ❌ **wallet_destroy** - Destroy wallet
- ❌ **wallet_add** - Add account to wallet (IMPORTANT)
- ❌ **wallet_add_watch** - Add watch-only account
- ❌ **wallet_balances** - Get wallet balances
- ❌ **wallet_change_seed** - Change wallet seed
- ❌ **wallet_contains** - Check if wallet contains account
- ❌ **wallet_export** - Export wallet
- ❌ **wallet_frontiers** - Get wallet frontiers
- ❌ **wallet_history** - Get wallet history
- ❌ **wallet_info** - Get wallet info
- ❌ **wallet_balance_total** - Get total wallet balance (alias)
- ❌ **wallet_key_valid** - Validate wallet key
- ❌ **wallet_ledger** - Get wallet ledger entries
- ❌ **wallet_lock** - Lock wallet
- ❌ **wallet_unlock** - Unlock wallet (alias for password_enter)
- ❌ **wallet_pending** - Get wallet pending blocks
- ❌ **wallet_receivable** - Get wallet receivable blocks
- ❌ **wallet_representative** - Get wallet representative
- ❌ **wallet_representative_set** - Set wallet representative
- ❌ **wallet_republish** - Republish wallet blocks
- ❌ **wallet_work_get** - Get work for wallet
- ❌ **password_change** - Change wallet password
- ❌ **password_enter** - Enter wallet password

#### Work Operations
- ❌ **work_generate** - Generate work (CRITICAL)
- ❌ **work_validate** - Validate work (IMPORTANT)
- ❌ **work_cancel** - Cancel work generation
- ❌ **work_get** - Get work for block
- ❌ **work_set** - Set work for block
- ❌ **work_peer_add** - Add work peer
- ❌ **work_peers** - List work peers
- ❌ **work_peers_clear** - Clear work peers

#### Configuration
- ❌ **receive_minimum** - Get minimum receive amount
- ❌ **receive_minimum_set** - Set minimum receive amount

#### Network Operations
- ❌ **republish** - Republish blocks
- ❌ **search_pending** - Search for pending blocks (deprecated)
- ❌ **search_receivable** - Search for receivable blocks
- ❌ **search_pending_all** - Search all pending (deprecated)
- ❌ **search_receivable_all** - Search all receivable

### LOW PRIORITY - Control/Admin Operations (16)

#### Bootstrap Operations
- ❌ **bootstrap** - Initiate bootstrap
- ❌ **bootstrap_any** - Bootstrap from any peer
- ❌ **bootstrap_lazy** - Lazy bootstrap
- ❌ **bootstrap_status** - Get bootstrap status
- ❌ **bootstrap_reset** - Reset bootstrap
- ❌ **bootstrap_priorities** - Get bootstrap priorities
- ❌ **populate_backlog** - Populate backlog

#### Network Control
- ❌ **keepalive** - Send keepalive to address

#### Node Management
- ❌ **node_id_delete** - Delete node ID
- ❌ **epoch_upgrade** - Upgrade epoch
- ❌ **database_txn_tracker** - Database transaction tracker

#### Cleanup Operations
- ❌ **unchecked_clear** - Clear unchecked blocks
- ❌ **stats_clear** - Clear statistics

---

## Recommended Implementation Order

### Phase 1: Critical Read-Only (5-6 endpoints)
1. account_history
2. frontiers
3. ledger
4. delegators
5. confirmation_quorum
6. confirmation_info

### Phase 2: Consensus & Monitoring (6 endpoints)
1. confirmation_active
2. confirmation_history
3. telemetry
4. stats
5. unchecked
6. unchecked_get

### Phase 3: Bulk Operations (6 endpoints)
1. accounts_frontiers
2. accounts_representatives
3. accounts_receivable
4. unopened
5. unchecked_keys
6. deterministic_key

### Phase 4: Critical State-Modifying (6 endpoints)
1. send
2. receive
3. process
4. work_generate
5. work_validate
6. sign

### Phase 5: Wallet Operations (23 endpoints)
All wallet_* operations

### Phase 6: Work & Network Operations (10 endpoints)
Remaining work_* and network operations

### Phase 7: Control Operations (16 endpoints)
Bootstrap and admin operations

---

## Categories Summary

| Category | V1 Total | V3 Implemented | Missing |
|----------|----------|----------------|---------|
| Account Info/Query | 15 | 9 | 6 |
| Block Operations | 10 | 6 | 4 |
| Ledger/Chain | 6 | 1 | 5 |
| Consensus | 5 | 1 | 4 |
| Representatives | 3 | 3 | 0 ✅ |
| Network/Peers | 3 | 2 | 1 |
| Wallet Operations | 23 | 0 | 23 |
| Work Operations | 8 | 0 | 8 |
| Utility/Conversion | 5 | 4 | 1 |
| Unchecked | 4 | 1 | 3 |
| Bootstrap | 7 | 0 | 7 |
| Control/Admin | 9 | 1 | 8 |
| State-Modifying | 15 | 1 | 14 |
| Stats/Monitoring | 3 | 0 | 3 |
| Other | 2 | 0 | 2 |

**TOTAL: 118 endpoints - 37 implemented = 81 missing**
