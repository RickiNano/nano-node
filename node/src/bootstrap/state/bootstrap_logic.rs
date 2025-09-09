use std::{collections::VecDeque, sync::Arc};

use rsnano_messages::AscPullReqType;
use rsnano_network::Channel;
use rsnano_nullable_clock::Timestamp;
use rsnano_types::{Account, BlockHash, Frontier};
use rsnano_utils::{
    container_info::ContainerInfo,
    stats::{StatsCollection, StatsSource},
};

use super::{
    running_query::QuerySource, CandidateAccounts, FrontierScan, PeerScoring, PriorityResult,
    RunningQueryContainer,
};
use crate::bootstrap::{
    state::{block_queue::BlockQueue, RunningQuery, VerifyResult},
    AscPullQuerySpec, BootstrapConfig,
};

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
        assert_eq!(logic.frontiers_stats.frontiers, 1);
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
