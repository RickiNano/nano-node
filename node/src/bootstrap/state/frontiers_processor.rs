use crate::bootstrap::{
    FrontierHeadInfo, FrontierScanConfig,
    state::{FrontierScan, RunningQuery, VerifyResult},
};
use rsnano_nullable_clock::Timestamp;
use rsnano_types::{Account, Frontier};
use rsnano_utils::{
    container_info::{ContainerInfo, ContainerInfoProvider},
    stats::{StatsCollection, StatsSource},
};
use std::collections::VecDeque;

pub(crate) struct FrontiersProcessor {
    pub(crate) frontier_scan: FrontierScan,
    frontiers_stats: FrontiersStats,

    /// Frontiers that were received from other nodes and that we need to check against our ledger
    frontiers_to_check: VecDeque<Vec<Frontier>>,
}

impl FrontiersProcessor {
    pub fn new(config: FrontierScanConfig) -> Self {
        Self {
            frontier_scan: FrontierScan::new(config),
            frontiers_stats: Default::default(),
            frontiers_to_check: Default::default(),
        }
    }

    pub fn heads(&self) -> Vec<FrontierHeadInfo> {
        self.frontier_scan.heads()
    }

    pub fn next(&mut self, now: Timestamp) -> Account {
        self.frontier_scan.next(now)
    }

    /// Returns true if the frontiers were valid
    pub fn process(&mut self, query: &RunningQuery, frontiers: Vec<Frontier>) -> bool {
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

    pub fn pop_frontiers_to_check(&mut self) -> Option<Vec<Frontier>> {
        self.frontiers_to_check.pop_front()
    }
}

impl Default for FrontiersProcessor {
    fn default() -> Self {
        Self::new(Default::default())
    }
}

impl StatsSource for FrontiersProcessor {
    fn collect_stats(&self, result: &mut StatsCollection) {
        self.frontiers_stats.collect_stats(result);
    }
}

impl ContainerInfoProvider for FrontiersProcessor {
    fn container_info(&self) -> ContainerInfo {
        self.frontier_scan.container_info()
    }
}

#[derive(Default)]
struct FrontiersStats {
    pub processed: u64,
    pub verified: u64,
    pub nothing_new: u64,
    pub invalid: u64,
    pub frontiers: u64,
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::bootstrap::state::{QuerySource, QueryType};

    #[test]
    fn empty_frontiers() {
        let mut processor = FrontiersProcessor::default();
        let query = running_query();

        let success = processor.process(&query, Vec::new());

        assert!(success);
        assert_eq!(processor.frontiers_stats.processed, 1);
        assert_eq!(processor.frontiers_stats.verified, 0);
        assert_eq!(processor.frontiers_stats.nothing_new, 1);
    }

    #[test]
    fn update_account_ranges() {
        let mut processor = FrontiersProcessor::default();
        let query = running_query();

        let success = processor.process(&query, vec![Frontier::new_test_instance()]);

        assert!(success);
        assert_eq!(processor.frontier_scan.total_requests_completed(), 1);
        assert_eq!(processor.frontiers_stats.processed, 1);
        assert_eq!(processor.frontiers_stats.verified, 1);
    }

    #[test]
    fn invalid_frontiers() {
        let mut processor = FrontiersProcessor::default();
        let query = running_query();

        let frontiers = vec![
            Frontier::new(3.into(), 100.into()),
            Frontier::new(1.into(), 200.into()), // descending order is invalid!
        ];

        let success = processor.process(&query, frontiers);

        assert!(!success);
        assert_eq!(processor.frontier_scan.total_requests_completed(), 0);
        assert_eq!(processor.frontiers_stats.processed, 1);
        assert_eq!(processor.frontiers_stats.invalid, 1);
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
