use std::sync::{Arc, Mutex};

use rsnano_messages::BlocksAckPayload;
use rsnano_network::ChannelId;
use rsnano_utils::stats::{DetailType, Direction, StatType, Stats};

use crate::{
    block_processing::{BlockContext, BlockProcessorQueue, BlockSource},
    bootstrap::{
        response_processor::block_queue::{AccountBlocks, NotifiableBlockQueue},
        state::{BootstrapState, PriorityDownResult, RunningQuery, VerifyResult},
    },
};
use tracing::trace;

pub(crate) struct BlockAckProcessor {
    state: Arc<Mutex<BootstrapState>>,
    stats: Arc<Stats>,
    block_processor_queue: Arc<BlockProcessorQueue>,
    block_queue: Arc<NotifiableBlockQueue>,
}

impl BlockAckProcessor {
    pub(crate) fn new(
        state: Arc<Mutex<BootstrapState>>,
        stats: Arc<Stats>,
        block_queue: Arc<NotifiableBlockQueue>,
        block_processor_queue: Arc<BlockProcessorQueue>,
    ) -> Self {
        Self {
            state,
            stats,
            block_queue,
            block_processor_queue,
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

        // TODO move this into a separate thread:

        while let Some(block) = blocks.pop_front() {
            trace!(block_hash = %block.hash(), query_id = query.id, "Process block");

            if blocks.is_empty() {
                // It's the last block submitted for this account chain, reset timestamp to allow more requests
                let stats = self.stats.clone();
                let state = self.state.clone();
                let account = query.account;
                self.block_processor_queue
                    .push(BlockContext::new_with_callback(
                        block,
                        BlockSource::Bootstrap,
                        // TODO: Use the correct channel ID
                        ChannelId::LOOPBACK,
                        Box::new(move |_, _, _| {
                            stats.inc(StatType::Bootstrap, DetailType::TimestampReset);
                            {
                                let mut guard = state.lock().unwrap();
                                guard.candidate_accounts.reset_last_request(&account);
                            }
                        }),
                    ));
            } else {
                self.block_processor_queue.push(BlockContext::new(
                    block,
                    BlockSource::Bootstrap,
                    ChannelId::LOOPBACK,
                ));
            }
        }
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
    use rsnano_types::Account;

    use crate::bootstrap::state::QueryType;

    use super::*;

    #[test]
    fn response_doesnt_match_query() {
        let state = Arc::new(Mutex::new(BootstrapState::default()));
        let stats = Arc::new(Stats::default());
        let block_processor_queue = Arc::new(BlockProcessorQueue::default());
        let block_queue = Arc::new(NotifiableBlockQueue::default());
        let processor =
            BlockAckProcessor::new(state, stats.clone(), block_queue, block_processor_queue);

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
        let block_processor_queue = Arc::new(BlockProcessorQueue::default());
        let block_queue = Arc::new(NotifiableBlockQueue::default());
        let processor =
            BlockAckProcessor::new(state, stats.clone(), block_queue, block_processor_queue);

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
