# RPC Migration Checklist

## Quick Reference: Old API → New API Mapping

This checklist tracks migration progress for all 118 RPC endpoints.

Legend:
- ✅ = Migrated and tested
- 🚧 = In progress
- ⬜ = Not started
- ⚠️ = Needs special attention

---

## Account Operations (21 total)

| Status | Old API Method | New API Handler | Phase | Notes |
|--------|----------------|-----------------|-------|-------|
| ✅ | `account_balance` | `handle_account_balance` | Done | |
| ✅ | `account_info` | `handle_account_info` | Done | |
| ✅ | `account_representative` | `handle_account_representative` | Done | |
| ✅ | `account_block_count` | `handle_account_block_count` | Done | ✅ Simple |
| ⬜ | `account_count` | `handle_account_count` | 1 | Simple |
| ⬜ | `account_create` | `handle_account_create` | 4 | Wallet modification |
| ⬜ | `account_get` | `handle_account_get` | 1 | Simple conversion |
| ⬜ | `account_history` | `handle_account_history` | 1 | Moderate - pagination |
| ⬜ | `account_key` | `handle_account_key` | 1 | Simple conversion |
| ⬜ | `account_list` | `handle_account_list` | 1 | Wallet query |
| ⬜ | `account_move` | `handle_account_move` | 4 | Wallet modification |
| ⬜ | `account_remove` | `handle_account_remove` | 4 | Wallet modification |
| ⬜ | `account_representative_set` | `handle_account_representative_set` | 4 | State change |
| ✅ | `account_weight` | `handle_account_weight` | Done | ✅ Simple |
| ⬜ | `accounts_balances` | `handle_accounts_balances` | Done | |
| ⬜ | `accounts_create` | `handle_accounts_create` | 4 | Wallet modification |
| ⬜ | `accounts_frontiers` | `handle_accounts_frontiers` | 1 | Moderate |
| ⬜ | `accounts_pending` | `handle_accounts_pending` | 1 | Legacy - use receivable |
| ⬜ | `accounts_receivable` | `handle_accounts_receivable` | 1 | Bulk query |
| ⬜ | `accounts_representatives` | `handle_accounts_representatives` | 1 | Bulk query |
| ⬜ | `unopened` | `handle_unopened` | 1 | Ledger query |

---

## Block Operations (14 total)

| Status | Old API Method | New API Handler | Phase | Notes |
|--------|----------------|-----------------|-------|-------|
| ✅ | `block_count` | `handle_block_count` | Done | |
| ✅ | `block_create` | `handle_block_create` | Done | Complex |
| ✅ | `block_info` | `handle_block_info` | Done | |
| ✅ | `block_account` | `handle_block_account` | Done | ✅ Simple |
| ⬜ | `block_confirm` | `handle_block_confirm` | 3 | Election trigger |
| ⬜ | `block_hash` | `handle_block_hash` | 1 | Utility |
| ⬜ | `blocks` | `handle_blocks` | 1 | Bulk query |
| ✅ | `blocks_info` | `handle_blocks_info` | Done | |
| ⬜ | `chain` | `handle_chain` | 1 | Successor/predecessor |
| ⬜ | `process` | `handle_process` | 4 | Critical - block processing |
| ⬜ | `pruned_exists` | `handle_pruned_exists` | 1 | Simple check |
| ⬜ | `republish` | `handle_republish` | 3 | Network operation |
| ⬜ | `unchecked` | `handle_unchecked` | 1 | Query unchecked |
| ⬜ | `unchecked_get` | `handle_unchecked_get` | 1 | Simple |

---

## Wallet Operations (20 total)

| Status | Old API Method | New API Handler | Phase | Notes |
|--------|----------------|-----------------|-------|-------|
| ⬜ | `password_change` | `handle_password_change` | 4 | Security |
| ⬜ | `password_enter` | `handle_password_enter` | 4 | Unlock |
| ⬜ | `password_valid` | `handle_password_valid` | 2 | Check lock status |
| ⬜ | `receive` | `handle_receive` | 4 | Transaction |
| ⬜ | `send` | `handle_send` | 4 | Transaction |
| ⬜ | `wallet_add` | `handle_wallet_add` | 4 | Add key |
| ⬜ | `wallet_add_watch` | `handle_wallet_add_watch` | 4 | Watch-only |
| ⬜ | `wallet_balances` | `handle_wallet_balances` | 2 | Query |
| ⬜ | `wallet_change_seed` | `handle_wallet_change_seed` | 4 | Security |
| ⬜ | `wallet_contains` | `handle_wallet_contains` | 2 | Check |
| ⬜ | `wallet_create` | `handle_wallet_create` | 4 | Create |
| ⬜ | `wallet_destroy` | `handle_wallet_destroy` | 4 | Dangerous |
| ⬜ | `wallet_export` | `handle_wallet_export` | 2 | Backup |
| ⬜ | `wallet_frontiers` | `handle_wallet_frontiers` | 2 | Query |
| ⬜ | `wallet_history` | `handle_wallet_history` | 2 | Query |
| ⬜ | `wallet_info` | `handle_wallet_info` | 2 | Query |
| ⬜ | `wallet_key_valid` | `handle_wallet_key_valid` | 2 | Check unlock |
| ⬜ | `wallet_ledger` | `handle_wallet_ledger` | 2 | Query |
| ⬜ | `wallet_lock` | `handle_wallet_lock` | 4 | Security |
| ⬜ | `wallet_pending` | `handle_wallet_pending` | 2 | Legacy query |

---

## Wallet Operations (continued)

| Status | Old API Method | New API Handler | Phase | Notes |
|--------|----------------|-----------------|-------|-------|
| ⬜ | `wallet_receivable` | `handle_wallet_receivable` | 2 | Query |
| ⬜ | `wallet_representative` | `handle_wallet_representative` | 2 | Query |
| ⬜ | `wallet_representative_set` | `handle_wallet_representative_set` | 4 | Modify |
| ⬜ | `wallet_republish` | `handle_wallet_republish` | 3 | Network |
| ⬜ | `wallet_seed` | `handle_wallet_seed` | 2 | Requires unlock |
| ⬜ | `wallet_work_get` | `handle_wallet_work_get` | 2 | Query work |

---

## Work/PoW Operations (9 total)

| Status | Old API Method | New API Handler | Phase | Notes |
|--------|----------------|-----------------|-------|-------|
| ⬜ | `receive_minimum` | `handle_receive_minimum` | 5 | Get threshold |
| ⬜ | `receive_minimum_set` | `handle_receive_minimum_set` | 5 | Set threshold |
| ⬜ | `work_cancel` | `handle_work_cancel` | 5 | Cancel generation |
| ⬜ | `work_generate` | `handle_work_generate` | 5 | Generate PoW |
| ⬜ | `work_get` | `handle_work_get` | 5 | Get precomputed |
| ⬜ | `work_peer_add` | `handle_work_peer_add` | 5 | Add peer |
| ⬜ | `work_peers` | `handle_work_peers` | 5 | List peers |
| ⬜ | `work_peers_clear` | `handle_work_peers_clear` | 5 | Clear peers |
| ⬜ | `work_set` | `handle_work_set` | 5 | Set precomputed |
| ⬜ | `work_validate` | `handle_work_validate` | 5 | Validate PoW |

---

## Network/Node Info (13 total)

| Status | Old API Method | New API Handler | Phase | Notes |
|--------|----------------|-----------------|-------|-------|
| ⬜ | `active_difficulty` | `handle_active_difficulty` | 1 | Current PoW difficulty |
| ✅ | `available_supply` | `handle_available_supply` | Done | |
| ⬜ | `keepalive` | `handle_keepalive` | 6 | Ping peer |
| ⬜ | `node_id` | `handle_node_id` | 1 | Get node ID |
| ⬜ | `node_id_delete` | `handle_node_id_delete` | 8 | Control - dangerous |
| ✅ | `peers` | `handle_peers` | Done | |
| ✅ | `representatives` | `handle_representatives` | Done | ✅ List reps |
| ⬜ | `representatives_online` | `handle_representatives_online` | 1 | Online reps |
| ⬜ | `stats` | `handle_stats` | 6 | Node statistics |
| ⬜ | `stats_clear` | `handle_stats_clear` | 6 | Clear stats |
| ⬜ | `stop` | `handle_stop` | 8 | Control - stop node |
| ⬜ | `telemetry` | `handle_telemetry` | 1 | Peer telemetry |
| ✅ | `uptime` | `handle_uptime` | Done | |

---

## Confirmation/Election (8 total)

| Status | Old API Method | New API Handler | Phase | Notes |
|--------|----------------|-----------------|-------|-------|
| ⬜ | `confirmation_active` | `handle_confirmation_active` | 3 | Active elections |
| ⬜ | `confirmation_history` | `handle_confirmation_history` | 3 | Past confirmations |
| ⬜ | `confirmation_info` | `handle_confirmation_info` | 3 | Detailed info |
| ⬜ | `confirmation_quorum` | `handle_confirmation_quorum` | 1 | Quorum info |
| ⬜ | `database_txn_tracker` | `handle_database_txn_tracker` | 6 | DB tracking |
| ⬜ | `election_statistics` | `handle_election_statistics` | 3 | Election stats |

---

## Ledger/Frontier Operations (7 total)

| Status | Old API Method | New API Handler | Phase | Notes |
|--------|----------------|-----------------|-------|-------|
| ⬜ | `delegators` | `handle_delegators` | 1 | List delegators |
| ⬜ | `delegators_count` | `handle_delegators_count` | 1 | Count delegators |
| ⬜ | `frontiers` | `handle_frontiers` | 1 | Account frontiers |
| ⬜ | `ledger` | `handle_ledger` | 1 | Query ledger |

---

## Pending/Receivable (6 total)

| Status | Old API Method | New API Handler | Phase | Notes |
|--------|----------------|-----------------|-------|-------|
| ⬜ | `pending` | `handle_pending` | 1 | Legacy - use receivable |
| ⬜ | `pending_exists` | `handle_pending_exists` | 1 | Check exists |
| ✅ | `receivable` | `handle_receivable` | Done | ✅ Get receivable |
| ✅ | `receivable_exists` | `handle_receivable_exists` | Done | ✅ Check exists |
| ⬜ | `search_pending` | `handle_search_pending` | 6 | Legacy search |
| ⬜ | `search_pending_all` | `handle_search_pending_all` | 6 | Search all |
| ⬜ | `search_receivable` | `handle_search_receivable` | 6 | Search receivable |
| ⬜ | `search_receivable_all` | `handle_search_receivable_all` | 6 | Search all |

---

## Bootstrap Operations (6 total)

| Status | Old API Method | New API Handler | Phase | Notes |
|--------|----------------|-----------------|-------|-------|
| ⬜ | `bootstrap` | `handle_bootstrap` | 7 | Legacy bootstrap |
| ⬜ | `bootstrap_any` | `handle_bootstrap_any` | 7 | Bootstrap any |
| ⬜ | `bootstrap_lazy` | `handle_bootstrap_lazy` | 7 | Lazy bootstrap |
| ⬜ | `bootstrap_priorities` | `handle_bootstrap_priorities` | 7 | Get priorities |
| ⬜ | `bootstrap_reset` | `handle_bootstrap_reset` | 7 | Reset attempts |
| ⬜ | `bootstrap_status` | `handle_bootstrap_status` | 7 | Get status |

---

## Utility/Conversion (8 total)

| Status | Old API Method | New API Handler | Phase | Notes |
|--------|----------------|-----------------|-------|-------|
| ⬜ | `deterministic_key` | `handle_deterministic_key` | 6 | Generate key |
| ⬜ | `epoch_upgrade` | `handle_epoch_upgrade` | 8 | Control - upgrade |
| ✅ | `key_create` | `handle_key_create` | Done | ✅ Generate keypair |
| ✅ | `key_expand` | `handle_key_expand` | Done | ✅ Expand key |
| ✅ | `nano_to_raw` | `handle_nano_to_raw` | Done | ✅ Convert units |
| ✅ | `raw_to_nano` | `handle_raw_to_nano` | Done | ✅ Convert units |
| ⬜ | `sign` | `handle_sign` | 4 | Sign block/hash |
| ⬜ | `unchecked_clear` | `handle_unchecked_clear` | 8 | Control - clear |
| ⬜ | `unchecked_keys` | `handle_unchecked_keys` | 1 | Get keys |
| ✅ | `validate_account_number` | `handle_validate_account_number` | Done | |
| ✅ | `version` | `handle_version` | Done | |

---

## Progress Summary

| Phase | Endpoints | Completed | Remaining | % Complete |
|-------|-----------|-----------|-----------|------------|
| Done | 23 | 23 | 0 | 100% |
| Phase 1 | 25 | 0 | 25 | 0% |
| Phase 2 | 14 | 0 | 14 | 0% |
| Phase 3 | 8 | 0 | 8 | 0% |
| Phase 4 | 18 | 0 | 18 | 0% |
| Phase 5 | 9 | 0 | 9 | 0% |
| Phase 6 | 11 | 0 | 11 | 0% |
| Phase 7 | 6 | 0 | 6 | 0% |
| Phase 8 | 4 | 0 | 4 | 0% |
| **TOTAL** | **118** | **23** | **95** | **19.5%** |

---

## Quick Start: First 10 Endpoints (COMPLETED ✅)

Completed in order for maximum impact:

1. ✅ `account_weight` - Simple, commonly used
2. ✅ `account_block_count` - Simple, commonly used
3. ✅ `block_account` - Simple, commonly used
4. ✅ `nano_to_raw` - Utility, simple
5. ✅ `raw_to_nano` - Utility, simple
6. ✅ `key_create` - Utility, simple
7. ✅ `key_expand` - Utility, simple
8. ✅ `receivable` - Important for users
9. ✅ `receivable_exists` - Pair with above
10. ✅ `representatives` - Network info

These 10 endpoints brought coverage to ~19.5% and handle many common use cases.

---

**Last Updated**: 2025-12-05
**Next Review**: After completing next batch of endpoints
