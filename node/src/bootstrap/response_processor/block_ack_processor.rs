use std::sync::{Arc, Mutex};
use tracing::trace;

use rsnano_messages::BlocksAckPayload;
use rsnano_utils::stats::{DetailType, Direction, StatType, Stats};

use crate::bootstrap::state::{
    BootstrapState, PriorityDownResult, RunningQuery, VerifyResult,
    block_queue::{AccountBlocks, NotifiableBlockQueue},
};

pub(crate) struct BlockAckProcessor {
    state: Arc<Mutex<BootstrapState>>,
    stats: Arc<Stats>,
    block_queue: Arc<NotifiableBlockQueue>,
}

impl BlockAckProcessor {
    pub(crate) fn new(
        state: Arc<Mutex<BootstrapState>>,
        stats: Arc<Stats>,
        block_queue: Arc<NotifiableBlockQueue>,
    ) -> Self {
        Self {
            state,
            stats,
            block_queue,
        }
    }

    pub fn process(&self, query: &RunningQuery, response: BlocksAckPayload) -> bool {
        trace!(
            query_id = query.id,
            blocks = response.blocks().len(),
            "Process response"
        );

        self.stats
            .inc(StatType::BootstrapProcess, DetailType::Blocks);

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
                self.stats
                    .inc(StatType::BootstrapVerifyBlocks, DetailType::Invalid);
                false
            }
        }
    }

    fn process_valid_blocks(&self, query: &RunningQuery, response: BlocksAckPayload) {
        self.stats
            .inc(StatType::BootstrapVerifyBlocks, DetailType::Ok);

        self.stats.add_dir(
            StatType::Bootstrap,
            DetailType::Blocks,
            Direction::In,
            response.blocks().len() as u64,
        );

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

    fn process_empty_response(&self, query: &RunningQuery) {
        self.stats
            .inc(StatType::BootstrapVerifyBlocks, DetailType::NothingNew);

        {
            let mut guard = self.state.lock().unwrap();
            match guard.candidate_accounts.priority_down(&query.account) {
                PriorityDownResult::Deprioritized => {
                    self.stats
                        .inc(StatType::BootstrapAccountSets, DetailType::Deprioritize);
                }
                PriorityDownResult::Erased => {
                    self.stats
                        .inc(StatType::BootstrapAccountSets, DetailType::Deprioritize);
                    self.stats.inc(
                        StatType::BootstrapAccountSets,
                        DetailType::PriorityEraseThreshold,
                    );
                }
                PriorityDownResult::AccountNotFound => {
                    self.stats.inc(
                        StatType::BootstrapAccountSets,
                        DetailType::DeprioritizeFailed,
                    );
                }
                PriorityDownResult::InvalidAccount => {}
            }

            guard.candidate_accounts.reset_last_request(&query.account);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::bootstrap::state::QueryType;
    use rsnano_types::Account;

    #[test]
    fn response_doesnt_match_query() {
        let state = Arc::new(Mutex::new(BootstrapState::default()));
        let stats = Arc::new(Stats::default());
        let block_queue = Arc::new(NotifiableBlockQueue::default());
        let processor = BlockAckProcessor::new(state, stats.clone(), block_queue);

        let query = RunningQuery::new_test_instance();
        let response = BlocksAckPayload::new_test_instance();
        let ok = processor.process(&query, response);
        assert!(!ok);
        assert_eq!(
            stats.count(
                StatType::BootstrapProcess,
                DetailType::Blocks,
                Direction::In
            ),
            1
        );
        assert_eq!(
            stats.count(
                StatType::BootstrapVerifyBlocks,
                DetailType::Invalid,
                Direction::In
            ),
            1
        );
    }

    #[test]
    fn handle_empty_response() {
        let state = Arc::new(Mutex::new(BootstrapState::default()));
        let stats = Arc::new(Stats::default());
        let block_queue = Arc::new(NotifiableBlockQueue::default());
        let processor = BlockAckProcessor::new(state, stats.clone(), block_queue);

        let account = Account::from(42);

        let query = RunningQuery {
            query_type: QueryType::BlocksByAccount,
            account,
            ..RunningQuery::new_test_instance()
        };

        let response = BlocksAckPayload::empty();
        let ok = processor.process(&query, response);
        assert!(ok);
        assert_eq!(
            stats.count(
                StatType::BootstrapProcess,
                DetailType::Blocks,
                Direction::In
            ),
            1
        );
        assert_eq!(
            stats.count(
                StatType::BootstrapVerifyBlocks,
                DetailType::NothingNew,
                Direction::In
            ),
            1
        );
    }
}
