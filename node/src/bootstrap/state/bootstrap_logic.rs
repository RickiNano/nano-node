use std::{collections::VecDeque, sync::Arc, time::Duration};

use rsnano_messages::{
    AccountInfoAckPayload, AscPullAck, AscPullAckType, AscPullReqType, BlocksAckPayload,
};
use rsnano_network::Channel;
use rsnano_nullable_clock::Timestamp;
use rsnano_types::{Account, BlockHash, Frontier};
use rsnano_utils::{
    container_info::ContainerInfo,
    stats::{StatsCollection, StatsSource},
};

use super::{
    CandidateAccounts, FrontierScan, PeerScoring, PriorityResult, RunningQueryContainer,
    running_query::QuerySource,
};
use crate::bootstrap::{
    AscPullQuerySpec, BootstrapConfig,
    state::{
        PriorityDownResult, QueryType, RunningQuery, VerifyResult,
        block_queue::{AccountBlocks, BlockQueue},
    },
};
use tracing::trace;

pub struct BootstrapLogic {
    pub candidate_accounts: CandidateAccounts,
    pub(crate) scoring: PeerScoring,
    pub(crate) running_queries: RunningQueryContainer,
    pub frontier_scan: FrontierScan,
    /// Frontiers that were received from other nodes and that we need to check against our ledger
    pub(crate) frontiers_to_check: VecDeque<Vec<Frontier>>,
    pub counters: BootstrapCounters,
    pub(crate) frontier_ack_processor_busy: bool,
    pub last_outdated_accounts: VecDeque<Account>,
    pub(crate) block_queue: BlockQueue,
    pub(crate) frontiers_stats: FrontiersStats,
    pub(crate) account_ack_stats: AccountAckStats,
    pub(crate) block_ack_stats: BlockAckStats,
    pub(crate) stopped: bool,
}

impl BootstrapLogic {
    pub fn new(config: BootstrapConfig) -> Self {
        let mut scoring = PeerScoring::new();
        scoring.set_channel_limit(config.channel_limit);

        Self {
            candidate_accounts: CandidateAccounts::new(config.candidate_accounts.clone()),
            scoring,
            frontier_scan: FrontierScan::new(config.frontier_scan.clone()),
            frontiers_to_check: VecDeque::new(),
            running_queries: RunningQueryContainer::default(),
            counters: BootstrapCounters::default(),
            frontier_ack_processor_busy: false,
            last_outdated_accounts: VecDeque::new(),
            block_queue: BlockQueue::default(),
            frontiers_stats: Default::default(),
            account_ack_stats: Default::default(),
            block_ack_stats: Default::default(),
            stopped: false,
        }
    }

    pub fn next_blocking_query(&self, channel: &Arc<Channel>) -> Option<AscPullQuerySpec> {
        let next = self.next_blocking();
        if !next.is_zero() {
            Some(Self::create_blocking_query(next, channel.clone()))
        } else {
            None
        }
    }

    fn create_blocking_query(next: BlockHash, channel: Arc<Channel>) -> AscPullQuerySpec {
        AscPullQuerySpec {
            query_id: 0, // TODO
            channel,
            req_type: AscPullReqType::account_info_by_hash(next),
            account: Account::ZERO,
            hash: next,
            cooldown_account: false,
        }
    }

    fn count_queries_by_hash(&self, hash: &BlockHash, source: QuerySource) -> usize {
        self.running_queries
            .iter_hash(hash)
            .filter(|i| i.source == source)
            .count()
    }

    pub fn next_priority(&mut self, now: Timestamp) -> PriorityResult {
        let next = self.candidate_accounts.next_priority(now, |account| {
            !self.block_queue.contains_account(account)
                && self
                    .running_queries
                    .count_by_account(account, QuerySource::Priority)
                    < 4
        });

        if next.account.is_zero() {
            return Default::default();
        }

        next
    }

    /* Waits for next available blocking block */
    pub fn next_blocking(&self) -> BlockHash {
        self.candidate_accounts
            .next_blocking(|hash| self.count_queries_by_hash(hash, QuerySource::Dependencies) == 0)
    }

    pub(crate) fn take_running_query_for(
        &mut self,
        response: &AscPullAck,
    ) -> Result<RunningQuery, ProcessError> {
        // Only process messages that have a known running query
        let Some(query) = self.running_queries.remove(response.id) else {
            return Err(ProcessError::NoRunningQueryFound);
        };

        if !query.is_valid_response_type(response) {
            return Err(ProcessError::InvalidResponseType);
        }

        Ok(query)
    }

    pub(crate) fn process_response(
        &mut self,
        response: AscPullAck,
        now: Timestamp,
    ) -> Result<ProcessInfo, ProcessError> {
        let query = self.take_running_query_for(&response)?;
        self.process_response_for_query(&query, response)
            .map(|_| ProcessInfo::new(&query, now))
    }

    pub(crate) fn process_response_for_query(
        &mut self,
        query: &RunningQuery,
        response: AscPullAck,
    ) -> Result<(), ProcessError> {
        let ok = match response.pull_type {
            AscPullAckType::Blocks(blocks) => self.process_blocks(query, blocks),
            AscPullAckType::AccountInfo(info) => self.process_account_ack(query, &info),
            AscPullAckType::Frontiers(frontiers) => self.process_frontiers(query, frontiers),
        };

        if ok {
            Ok(())
        } else {
            Err(ProcessError::InvalidResponse)
        }
    }

    // Frontiers ack handling:
    //********************************************************************************

    pub fn frontiers_processed(&mut self, outdated: &OutdatedAccounts) {
        self.counters.received_frontiers += outdated.fontiers_received;
        self.counters.outdated_accounts_found += outdated.accounts.len();

        for account in &outdated.accounts {
            // Use the lowest possible priority here
            self.candidate_accounts
                .priority_set(account, CandidateAccounts::PRIORITY_CUTOFF);

            self.last_outdated_accounts.push_back(*account);
            if self.last_outdated_accounts.len() > 20 {
                self.last_outdated_accounts.pop_front();
            }
        }
    }

    pub fn set_frontier_checker_overfill(&mut self, overfill: bool) {
        self.frontier_ack_processor_busy = overfill;
    }

    /// Returns true if the frontiers were valid
    pub(crate) fn process_frontiers(
        &mut self,
        query: &RunningQuery,
        frontiers: Vec<Frontier>,
    ) -> bool {
        self.frontiers_stats.processed += 1;

        let valid_frontiers = match query.verify_frontiers(&frontiers) {
            VerifyResult::Ok => {
                self.frontiers_stats.verified += 1;
                self.frontiers_stats.frontiers += frontiers.len() as u64;
                self.frontier_scan.process(query.start.into(), &frontiers);
                self.frontiers_to_check.push_back(frontiers);
                true
            }
            VerifyResult::NothingNew => {
                self.frontiers_stats.nothing_new += 1;
                // OK, but nothing to do
                true
            }
            VerifyResult::Invalid => {
                self.frontiers_stats.invalid += 1;
                false
            }
        };
        valid_frontiers
    }

    // account ack handling:
    //********************************************************************************

    pub(crate) fn process_account_ack(
        &mut self,
        query: &RunningQuery,
        response: &AccountInfoAckPayload,
    ) -> bool {
        if response.account.is_zero() {
            self.account_ack_stats.empty += 1;
            // OK, but nothing to do
            return true;
        }

        self.account_ack_stats.process += 1;

        // Prioritize account containing the dependency
        self.update_dependency(&query.hash, response.account);
        self.prioritize(&response.account);

        // OK, no way to verify the response
        true
    }

    fn update_dependency(&mut self, hash: &BlockHash, dep_account: Account) {
        let updated = self.candidate_accounts.dependency_update(hash, dep_account);

        if updated > 0 {
            self.account_ack_stats.dependency_update += updated as u64;
        } else {
            self.account_ack_stats.dependency_update_failed += 1;
        }
    }

    fn prioritize(&mut self, account: &Account) {
        // Use the lowest possible priority here
        if self
            .candidate_accounts
            .priority_set(account, CandidateAccounts::PRIORITY_CUTOFF)
        {
            self.account_ack_stats.priority_insert += 1;
        } else {
            self.account_ack_stats.prioritize_failed += 1;
        };
    }

    // block ack handling:
    //********************************************************************************
    pub(crate) fn process_blocks(
        &mut self,
        query: &RunningQuery,
        response: BlocksAckPayload,
    ) -> bool {
        trace!(
            query_id = query.id,
            blocks = response.blocks().len(),
            "Process response"
        );

        self.block_ack_stats.process += 1;

        let result = query.verify_blocks(&response);
        match result {
            VerifyResult::Ok => {
                self.process_valid_blocks(query, response);
                true
            }
            VerifyResult::NothingNew => {
                self.process_empty_response(query);
                true
            }
            VerifyResult::Invalid => {
                self.block_ack_stats.invalid += 1;
                false
            }
        }
    }

    fn process_valid_blocks(&mut self, query: &RunningQuery, response: BlocksAckPayload) {
        self.block_ack_stats.verified += 1;
        self.block_ack_stats.blocks += response.blocks().len() as u64;

        let mut blocks = response.take_blocks();

        // Avoid re-processing the block we already have
        assert!(blocks.len() >= 1);
        if blocks.front().unwrap().hash() == query.start.into() {
            blocks.pop_front();
        }

        self.block_queue.insert(AccountBlocks {
            account: query.account,
            query_id: query.id,
            blocks: blocks.clone(),
        });
    }

    fn process_empty_response(&mut self, query: &RunningQuery) {
        self.block_ack_stats.nothing_new += 1;
        {
            match self.candidate_accounts.priority_down(&query.account) {
                PriorityDownResult::Deprioritized => {
                    self.block_ack_stats.deprioritize += 1;
                }
                PriorityDownResult::Erased => {
                    self.block_ack_stats.deprioritize += 1;
                    self.block_ack_stats.priority_erase_theshold += 1;
                }
                PriorityDownResult::AccountNotFound => {
                    self.block_ack_stats.deprioritize_failed += 1;
                }
                PriorityDownResult::InvalidAccount => {}
            }

            self.candidate_accounts.reset_last_request(&query.account);
        }
    }

    //********************************************************************************

    pub fn container_info(&self) -> ContainerInfo {
        ContainerInfo::builder()
            .leaf(
                "tags",
                self.running_queries.len(),
                RunningQueryContainer::ELEMENT_SIZE,
            )
            .node("accounts", self.candidate_accounts.container_info())
            .node("frontiers", self.frontier_scan.container_info())
            .node("peers", self.scoring.container_info())
            .finish()
    }
}

impl Default for BootstrapLogic {
    fn default() -> Self {
        Self::new(Default::default())
    }
}

impl StatsSource for BootstrapLogic {
    fn collect_stats(&self, result: &mut StatsCollection) {
        self.frontiers_stats.collect_stats(result)
    }
}

#[derive(Debug)]
pub(crate) enum ProcessError {
    NoRunningQueryFound,
    InvalidResponseType,
    InvalidResponse,
}

pub(crate) struct ProcessInfo {
    pub query_type: QueryType,
    pub response_time: Duration,
}

impl ProcessInfo {
    pub fn new(query: &RunningQuery, now: Timestamp) -> Self {
        Self {
            query_type: query.query_type,
            response_time: query.sent.elapsed(now),
        }
    }
}

#[derive(Default)]
pub(crate) struct FrontiersStats {
    pub(crate) processed: u64,
    pub(crate) verified: u64,
    pub(crate) nothing_new: u64,
    pub(crate) invalid: u64,
    pub(crate) frontiers: u64,
}

impl StatsSource for FrontiersStats {
    fn collect_stats(&self, result: &mut StatsCollection) {
        result.insert("bootstrap_process", "frontiers", self.processed);
        result.insert("bootstrap_verify_frontiers", "ok", self.verified);
        result.insert(
            "bootstrap_verify_frontiers",
            "nothing_new",
            self.nothing_new,
        );
        result.insert("bootstrap_verify_frontiers", "invalid", self.invalid);
        result.insert("bootstrap", "frontiers", self.frontiers);
    }
}

#[derive(Default)]
pub(crate) struct AccountAckStats {
    empty: u64,
    process: u64,
    dependency_update: u64,
    dependency_update_failed: u64,
    priority_insert: u64,
    prioritize_failed: u64,
}

impl StatsSource for AccountAckStats {
    fn collect_stats(&self, result: &mut StatsCollection) {
        result.insert("bootstrap_process", "account_info", self.process);
        result.insert("bootstrap_process", "account_info_empty", self.empty);
        result.insert(
            "bootstrap_account_sets",
            "dependency_update",
            self.dependency_update,
        );
        result.insert(
            "bootstrap_account_sets",
            "dependency_update_failed",
            self.dependency_update_failed,
        );
        result.insert(
            "bootstrap_account_sets",
            "priority_insert",
            self.priority_insert,
        );
        result.insert(
            "bootstrap_account_sets",
            "prioritize_failed",
            self.prioritize_failed,
        );
    }
}

#[derive(Default)]
pub(crate) struct BlockAckStats {
    process: u64,
    invalid: u64,
    verified: u64,
    blocks: u64,
    nothing_new: u64,
    deprioritize: u64,
    priority_erase_theshold: u64,
    deprioritize_failed: u64,
}

impl StatsSource for BlockAckStats {
    fn collect_stats(&self, result: &mut StatsCollection) {
        result.insert("bootstrap_process", "blocks", self.process);
        result.insert("bootstrap_verify_blocks", "invalid", self.invalid);
        result.insert("bootstrap_verify_blocks", "ok", self.verified);
        result.insert("bootstrap", "blocks", self.blocks);
        result.insert("bootstrap_verify_blocks", "nothing_new", self.nothing_new);
        result.insert("bootstrap_account_sets", "deprioritize", self.deprioritize);
        result.insert(
            "bootstrap_account_sets",
            "priority_erase_threshold",
            self.priority_erase_theshold,
        );
        result.insert(
            "bootstrap_account_sets",
            "deprioritize_failed",
            self.deprioritize_failed,
        );
    }
}

#[derive(Default, Clone)]
pub struct BootstrapCounters {
    pub received_frontiers: usize,
    pub outdated_accounts_found: usize,
}

#[derive(Default, Debug, PartialEq, Eq)]
pub struct OutdatedAccounts {
    pub accounts: Vec<Account>,
    /// Accounts that exist but are outdated
    pub outdated: usize,
    /// Accounts that don't exist but have pending blocks in the ledger
    pub pending: usize,
    /// Total count of received frontiers
    pub fontiers_received: usize,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::bootstrap::state::QueryType;

    mod frontiers {
        use super::*;

        #[test]
        fn empty_frontiers() {
            let mut logic = BootstrapLogic::default();
            let query = running_query();

            let success = logic.process_frontiers(&query, Vec::new());

            assert!(success);
            assert_eq!(logic.frontiers_stats.processed, 1);
            assert_eq!(logic.frontiers_stats.verified, 0);
            assert_eq!(logic.frontiers_stats.nothing_new, 1);
        }

        #[test]
        fn update_account_ranges() {
            let mut logic = BootstrapLogic::default();
            let query = running_query();

            let success = logic.process_frontiers(&query, vec![Frontier::new_test_instance()]);

            assert!(success);
            assert_eq!(logic.frontier_scan.total_requests_completed(), 1);
            assert_eq!(logic.frontiers_stats.processed, 1);
            assert_eq!(logic.frontiers_stats.verified, 1);
        }

        #[test]
        fn invalid_frontiers() {
            let mut logic = BootstrapLogic::default();
            let query = running_query();

            let frontiers = vec![
                Frontier::new(3.into(), 100.into()),
                Frontier::new(1.into(), 200.into()), // descending order is invalid!
            ];

            let success = logic.process_frontiers(&query, frontiers);

            assert!(!success);
            assert_eq!(logic.frontier_scan.total_requests_completed(), 0);
            assert_eq!(logic.frontiers_stats.processed, 1);
            assert_eq!(logic.frontiers_stats.invalid, 1);
        }

        fn running_query() -> RunningQuery {
            RunningQuery {
                source: QuerySource::Frontiers,
                query_type: QueryType::Frontiers,
                start: 1.into(),
                ..RunningQuery::new_test_instance()
            }
        }
    }

    mod account_ack {
        use super::*;
        #[test]
        fn empty_response() {
            let mut logic = BootstrapLogic::default();
            let query = RunningQuery::new_test_instance();

            let response = AccountInfoAckPayload {
                account: Account::ZERO,
                ..AccountInfoAckPayload::new_test_instance()
            };

            assert!(logic.process_account_ack(&query, &response));
            assert_eq!(logic.account_ack_stats.empty, 1);
            assert_eq!(logic.account_ack_stats.process, 0);
            assert_eq!(logic.candidate_accounts.priority_len(), 0);
        }

        #[test]
        fn when_not_blocked_should_only_prioritize() {
            let mut logic = BootstrapLogic::default();
            let query = RunningQuery::new_test_instance();
            let response = AccountInfoAckPayload::new_test_instance();

            assert!(logic.process_account_ack(&query, &response));

            assert_eq!(logic.account_ack_stats.process, 1);
            assert!(logic.candidate_accounts.prioritized(&response.account));
            assert_eq!(logic.account_ack_stats.dependency_update_failed, 1);
            assert_eq!(logic.account_ack_stats.priority_insert, 1);
        }

        #[test]
        fn update_dependency() {
            let mut logic = BootstrapLogic::default();
            let blocked_account = Account::from(100);
            let unknown_source = BlockHash::from(42);
            let source_account = Account::from(200);

            let query = RunningQuery {
                hash: unknown_source,
                ..RunningQuery::new_test_instance()
            };

            let response = AccountInfoAckPayload {
                account: source_account,
                ..AccountInfoAckPayload::new_test_instance()
            };

            logic
                .candidate_accounts
                .priority_set_initial(&blocked_account);

            logic.candidate_accounts.block(
                blocked_account,
                unknown_source,
                Timestamp::new_test_instance(),
            );

            assert!(logic.process_account_ack(&query, &response));

            assert!(logic.candidate_accounts.blocked(&blocked_account));
            assert!(logic.candidate_accounts.prioritized(&source_account));
            let query = logic.next_priority(Timestamp::new_test_instance());
            assert_eq!(query.account, source_account);
            assert_eq!(logic.account_ack_stats.dependency_update, 1);
            assert_eq!(logic.account_ack_stats.priority_insert, 1);
        }

        #[test]
        fn dependency_update_fails() {
            let mut logic = BootstrapLogic::default();

            let blocked_account = Account::from(100);
            let unknown_source = BlockHash::from(42);
            let source_account = Account::from(200);

            let query = RunningQuery {
                hash: unknown_source,
                ..RunningQuery::new_test_instance()
            };

            let response = AccountInfoAckPayload {
                account: source_account,
                ..AccountInfoAckPayload::new_test_instance()
            };

            logic
                .candidate_accounts
                .priority_set_initial(&blocked_account);
            logic.candidate_accounts.block(
                blocked_account,
                unknown_source,
                Timestamp::new_test_instance(),
            );
            logic
                .candidate_accounts
                .dependency_update(&unknown_source, source_account);
            logic
                .candidate_accounts
                .priority_set_initial(&source_account);

            assert!(logic.process_account_ack(&query, &response));

            assert_eq!(logic.account_ack_stats.dependency_update_failed, 1);
            assert_eq!(logic.account_ack_stats.prioritize_failed, 1);
        }
    }

    mod block_ack {
        use super::*;

        #[test]
        fn response_doesnt_match_query() {
            let mut logic = BootstrapLogic::default();

            let query = RunningQuery::new_test_instance();
            let response = BlocksAckPayload::new_test_instance();
            let ok = logic.process_blocks(&query, response);
            assert!(!ok);
            assert_eq!(logic.block_ack_stats.process, 1);
            assert_eq!(logic.block_ack_stats.invalid, 1);
        }

        #[test]
        fn handle_empty_response() {
            let mut logic = BootstrapLogic::default();
            let account = Account::from(42);

            let query = RunningQuery {
                query_type: QueryType::BlocksByAccount,
                account,
                ..RunningQuery::new_test_instance()
            };

            let response = BlocksAckPayload::empty();
            let ok = logic.process_blocks(&query, response);
            assert!(ok);
            assert_eq!(logic.block_ack_stats.process, 1);
            assert_eq!(logic.block_ack_stats.nothing_new, 1);
        }
    }
}
