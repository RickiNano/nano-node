use crate::{
    block_processing::LedgerEvent,
    bootstrap::state::{BootstrapState, block_queue::NotifiableBlockQueue},
    ledger_event_processor::LedgerEventProcessorPlugin,
};
use std::sync::{Arc, Mutex};

pub(crate) struct BootstrapLedgerEventProcessor {
    block_queue: Arc<NotifiableBlockQueue>,
    state: Arc<Mutex<BootstrapState>>,
}

impl BootstrapLedgerEventProcessor {
    pub(crate) fn new(
        block_queue: Arc<NotifiableBlockQueue>,
        state: Arc<Mutex<BootstrapState>>,
    ) -> Self {
        Self { block_queue, state }
    }
}

impl LedgerEventProcessorPlugin for BootstrapLedgerEventProcessor {
    fn process(&mut self, event: &LedgerEvent) {
        let LedgerEvent::BlocksProcessed(results) = event else {
            return;
        };

        let mut should_notify = false;
        {
            let mut guard = self.block_queue.queue.lock().unwrap();
            let queue = &mut guard.0;

            for result in results {
                if result.status.is_ok() {
                    let info = queue.processed(&result.block.hash());
                    if let Some(account) = info.account {
                        should_notify = true;

                        if info.was_last {
                            self.state
                                .lock()
                                .unwrap()
                                .candidate_accounts
                                .reset_last_request(&account);
                        }
                    }
                } else {
                    let was_enqueued = queue.processing_failed(&result.block.hash());
                    if was_enqueued {
                        should_notify = true;
                    }
                }
            }
        }

        if should_notify {
            self.block_queue.notify.notify_one();
        }
    }
}
