use std::sync::{Arc, mpsc};

use rsnano_types::WorkRequest;
use rsnano_wallet::Wallets;

use crate::work::WorkFactory;

pub(crate) struct WalletWorkProvider {
    wallets: Arc<Wallets>,
    queue: mpsc::Receiver<WorkRequest>,
    work_factory: Arc<WorkFactory>,
}

impl WalletWorkProvider {
    pub(crate) fn new(
        wallets: Arc<Wallets>,
        queue: mpsc::Receiver<WorkRequest>,
        work_factory: Arc<WorkFactory>,
    ) -> Self {
        Self {
            wallets,
            queue,
            work_factory,
        }
    }

    pub fn run(self) {
        while let Ok(request) = self.queue.recv() {
            let root = request.root;
            let wallets = Arc::downgrade(&self.wallets);
            let request = request.with_callback(Box::new(move |work| {
                if let Some(w) = wallets.upgrade() {
                    w.provide_work(&root, work);
                }
            }));

            self.work_factory.generate_work_async(request);
        }
    }
}
