use rsnano_ledger::Ledger;
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
        match event {
            LedgerEvent::BlocksProcessed(result) => {
                println!("Fork detected: {:?}", result[0].block.qualified_root());

                self.ledger.mark_fork(
                    &result[0].block.qualified_root(),
                    self.ledger_snapshots.get_current_snapshot_number(),
                );
            }
            _ => println!("Error!"),
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

    #[test]
    fn put_root_and_snapshot_number_in_forks_store() {
        let ledger = Ledger::new_null();
        let ledger_snapshots = LedgerSnapshots::new_null();
        let mut fork_detector = ForkDetector::new(ledger.into(), ledger_snapshots.into());
        let block = Block::new_test_instance();
        let root = block.qualified_root();

        println!("ROOT: {:?}", root);

        let processed_results = ProcessedResult {
            block,
            source: BlockSource::Live,
            status: Err(BlockError::Fork),
            saved_block: None,
        };

        fork_detector.process(&LedgerEvent::BlocksProcessed(vec![processed_results]));

        assert!(
            fork_detector
                .ledger
                .store
                .forks
                .contains(&fork_detector.ledger.store.env.begin_read(), &root)
        );
    }
}
