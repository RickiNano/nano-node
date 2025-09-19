use rsnano_ledger::{BlockError, Ledger};
use std::sync::Arc;
use tracing::warn;

use crate::{
    block_processing::LedgerEvent, ledger_event_processor::LedgerEventProcessorPlugin,
    ledger_snapshots::LedgerSnapshots,
};

pub(crate) struct ForkDetector {
    ledger: Arc<Ledger>,
    ledger_snapshots: Arc<LedgerSnapshots>,
}

impl ForkDetector {
    pub(crate) fn new(ledger: Arc<Ledger>, ledger_snapshots: Arc<LedgerSnapshots>) -> Self {
        Self {
            ledger,
            ledger_snapshots,
        }
    }
}

impl LedgerEventProcessorPlugin for ForkDetector {
    fn process(&mut self, event: &LedgerEvent) {
        if let LedgerEvent::BlocksProcessed(results) = event {
            for result in results {
                if result.status == Err(BlockError::Fork) {
                    warn!("Fork detected: {:?}", result.block.qualified_root());

                    self.ledger.mark_fork(
                        &result.block.qualified_root(),
                        self.ledger_snapshots.get_current_snapshot_number(),
                    );
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::{
        block_processing::{BlockSource, LedgerEvent, ProcessedResult},
        ledger_event_processor::LedgerEventProcessorPlugin,
        ledger_snapshots::{LedgerSnapshots, fork_detector::ForkDetector},
    };
    use rsnano_ledger::{BlockError, Ledger};
    use rsnano_types::Block;
    use std::sync::Arc;

    #[test]
    fn marks_a_forked_block_in_the_ledger() {
        let ledger = Arc::new(Ledger::new_null());
        let ledger_snapshots = LedgerSnapshots::new_null();
        let snapshot_number = ledger_snapshots.get_current_snapshot_number();
        let mut fork_detector = ForkDetector::new(ledger.clone(), ledger_snapshots.into());
        let block = Block::new_test_instance();
        let root = block.qualified_root();

        let processed_results = ProcessedResult {
            block,
            source: BlockSource::Live,
            status: Err(BlockError::Fork),
            saved_block: None,
        };

        fork_detector.process(&LedgerEvent::BlocksProcessed(vec![processed_results]));

        assert_eq!(
            ledger
                .store
                .forks
                .get(&ledger.store.env.begin_read(), &root),
            Some(snapshot_number)
        );
    }

    #[test]
    fn can_mark_multiple_forks_in_one_go() {
        let ledger = Arc::new(Ledger::new_null());
        let ledger_snapshots = LedgerSnapshots::new_null();
        let snapshot_number = ledger_snapshots.get_current_snapshot_number();
        let mut fork_detector = ForkDetector::new(ledger.clone(), ledger_snapshots.into());
        let block1 = Block::new_test_instance_with_key(1);
        let block2 = Block::new_test_instance_with_key(2);
        let root1 = block1.qualified_root();
        let root2 = block2.qualified_root();

        let processed_result1 = ProcessedResult {
            block: block1,
            source: BlockSource::Live,
            status: Err(BlockError::Fork),
            saved_block: None,
        };

        let processed_result2 = ProcessedResult {
            block: block2,
            source: BlockSource::Live,
            status: Err(BlockError::Fork),
            saved_block: None,
        };

        fork_detector.process(&LedgerEvent::BlocksProcessed(vec![
            processed_result1,
            processed_result2,
        ]));

        assert_eq!(
            ledger
                .store
                .forks
                .get(&ledger.store.env.begin_read(), &root1),
            Some(snapshot_number)
        );

        assert_eq!(
            ledger
                .store
                .forks
                .get(&ledger.store.env.begin_read(), &root2),
            Some(snapshot_number)
        );
    }

    #[test]
    fn ignores_blocks_without_fork() {
        let ledger = Arc::new(Ledger::new_null());
        let ledger_snapshots = LedgerSnapshots::new_null();
        let mut fork_detector = ForkDetector::new(ledger.clone(), ledger_snapshots.into());
        let block = Block::new_test_instance();
        let root = block.qualified_root();

        let processed_results = ProcessedResult {
            block,
            source: BlockSource::Live,
            status: Err(BlockError::GapPrevious),
            saved_block: None,
        };

        fork_detector.process(&LedgerEvent::BlocksProcessed(vec![processed_results]));

        assert_eq!(
            ledger
                .store
                .forks
                .get(&ledger.store.env.begin_read(), &root),
            None
        );
    }
}
