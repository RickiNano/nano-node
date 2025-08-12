use std::sync::{Arc, Mutex};

use rsnano_core::{
    utils::{CancellationToken, Tickable},
    Block,
};
use rsnano_ledger::{Ledger, LedgerSet};

use super::UncheckedMap;
use crate::block_processing::{BlockContext, BlockProcessorQueue, BlockSource};
use rsnano_network::ChannelId;

/// Re-enqueues an unchecked block when its missing dependency block got inserted into the ledger
pub struct UncheckedBlockReenqueuer {
    unchecked: Arc<Mutex<UncheckedMap>>,
    ledger: Arc<Ledger>,
    process_queue: Arc<BlockProcessorQueue>,
    satisfied_blocks: Vec<Block>,
}

impl UncheckedBlockReenqueuer {
    pub fn new(
        unchecked: Arc<Mutex<UncheckedMap>>,
        ledger: Arc<Ledger>,
        process_queue: Arc<BlockProcessorQueue>,
    ) -> Self {
        Self {
            unchecked,
            ledger,
            process_queue,
            satisfied_blocks: Vec::new(),
        }
    }

    fn find_satisfied_blocks(&mut self) {
        let mut unchecked = self.unchecked.lock().unwrap();
        // TODO: check all blocks, but in batches!
        let Some(dep_hash) = unchecked
            .iter()
            .map(|(dependency_hash, _)| *dependency_hash)
            .next()
        else {
            return;
        };

        if self.ledger.any().block_exists(&dep_hash) {
            unchecked.pop_dependend_blocks(dep_hash, &mut self.satisfied_blocks);
        }
    }

    fn enqueue_satisfied_blocks(&mut self) {
        for block in self.satisfied_blocks.drain(..) {
            self.process_queue.push(BlockContext::new(
                block,
                BlockSource::Unchecked,
                ChannelId::LOOPBACK,
            ));
        }
    }
}

impl Tickable for UncheckedBlockReenqueuer {
    fn tick(&mut self, _cancel_token: &CancellationToken) {
        self.find_satisfied_blocks();
        self.enqueue_satisfied_blocks();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::block_processing::BlockSource;
    use rsnano_core::{Block, BlockHash, SavedBlock};

    #[test]
    fn reenqueue_satisfied_block() {
        let dependency_block = SavedBlock::new_test_instance();
        let unchecked_block = Block::new_test_instance_with_key(2);

        let unchecked = Arc::new(Mutex::new(UncheckedMap::default()));
        unchecked
            .lock()
            .unwrap()
            .put(dependency_block.hash(), unchecked_block.clone());

        let ledger = Arc::new(Ledger::new_null_builder().block(&dependency_block).finish());
        let process_queue = Arc::new(BlockProcessorQueue::default());
        let mut reenqueuer =
            UncheckedBlockReenqueuer::new(unchecked, ledger, process_queue.clone());

        reenqueuer.tick(&CancellationToken::new_null());

        assert_eq!(process_queue.total_queue_len(), 1, "enqueued blocks count");
        let enqueued = process_queue.pop_blocking().unwrap().pop_front().unwrap();
        assert_eq!(enqueued.block.hash(), unchecked_block.hash());
        assert_eq!(enqueued.source, BlockSource::Unchecked);
    }

    #[test]
    fn dont_enqueue_if_dependency_still_isnt_satisfied() {
        let dependency_hash = BlockHash::from(123);
        let unchecked_block = Block::new_test_instance_with_key(2);

        let unchecked = Arc::new(Mutex::new(UncheckedMap::default()));
        unchecked
            .lock()
            .unwrap()
            .put(dependency_hash, unchecked_block.clone());

        let ledger = Arc::new(Ledger::new_null());
        let process_queue = Arc::new(BlockProcessorQueue::default());
        let mut reenqueuer =
            UncheckedBlockReenqueuer::new(unchecked, ledger, process_queue.clone());

        reenqueuer.tick(&CancellationToken::new_null());

        assert_eq!(process_queue.total_queue_len(), 0, "enqueued blocks count");
    }
}
