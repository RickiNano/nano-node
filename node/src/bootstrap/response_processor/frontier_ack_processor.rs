use std::sync::{Arc, Mutex};

use rsnano_types::Frontier;
use rsnano_utils::stats::{DetailType, Direction, StatType, Stats};

use crate::bootstrap::state::{BootstrapLogic, RunningQuery, VerifyResult};

/// Processes responses to AscPullReqs by the frontier scan
pub(crate) struct FrontierAckProcessor {
    stats: Arc<Stats>,
    logic: Arc<Mutex<BootstrapLogic>>,
}

impl FrontierAckProcessor {
    pub(crate) fn new(stats: Arc<Stats>, state: Arc<Mutex<BootstrapLogic>>) -> Self {
        Self {
            stats,
            logic: state,
        }
    }

    /// Returns true if the frontiers were valid
    pub fn process(&self, query: &RunningQuery, frontiers: Vec<Frontier>) -> bool {
        self.stats
            .inc(StatType::BootstrapProcess, DetailType::Frontiers);

        let valid_frontiers = match query.verify_frontiers(&frontiers) {
            VerifyResult::Ok => {
                self.stats
                    .inc(StatType::BootstrapVerifyFrontiers, DetailType::Ok);
                self.process_valid_frontiers(query, frontiers);
                true
            }
            VerifyResult::NothingNew => {
                self.stats
                    .inc(StatType::BootstrapVerifyFrontiers, DetailType::NothingNew);
                // OK, but nothing to do
                true
            }
            VerifyResult::Invalid => {
                self.stats
                    .inc(StatType::BootstrapVerifyFrontiers, DetailType::Invalid);
                false
            }
        };
        valid_frontiers
    }

    fn process_valid_frontiers(&self, query: &RunningQuery, frontiers: Vec<Frontier>) {
        self.stats.add_dir(
            StatType::Bootstrap,
            DetailType::Frontiers,
            Direction::In,
            frontiers.len() as u64,
        );

        self.stats
            .inc(StatType::BootstrapFrontierScan, DetailType::Process);

        let mut guard = self.logic.lock().unwrap();
        guard.frontier_scan.process(query.start.into(), &frontiers);
        guard.frontiers_to_check.push_back(frontiers);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::bootstrap::state::{QuerySource, QueryType};

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
        let fixture = create_fixture();
        let query = running_query();

        let frontiers = vec![
            Frontier::new(3.into(), 100.into()),
            Frontier::new(1.into(), 200.into()), // descending order is invalid!
        ];

        let success = fixture.processor.process(&query, frontiers);

        assert!(!success);
        assert_eq!(
            fixture
                .logic
                .lock()
                .unwrap()
                .frontier_scan
                .total_requests_completed(),
            0
        );
        assert_eq!(
            fixture.stats.count(
                StatType::BootstrapProcess,
                DetailType::Frontiers,
                Direction::In
            ),
            1
        );
        assert_eq!(
            fixture.stats.count(
                StatType::BootstrapVerifyFrontiers,
                DetailType::Invalid,
                Direction::In
            ),
            1
        );
    }

    fn create_fixture() -> Fixture {
        let stats = Arc::new(Stats::default());
        let logic = Arc::new(Mutex::new(BootstrapLogic::default()));
        let processor = FrontierAckProcessor::new(stats.clone(), logic.clone());

        Fixture {
            stats,
            processor,
            logic,
        }
    }

    fn running_query() -> RunningQuery {
        RunningQuery {
            source: QuerySource::Frontiers,
            query_type: QueryType::Frontiers,
            start: 1.into(),
            ..RunningQuery::new_test_instance()
        }
    }

    struct Fixture {
        stats: Arc<Stats>,
        processor: FrontierAckProcessor,
        logic: Arc<Mutex<BootstrapLogic>>,
    }
}
