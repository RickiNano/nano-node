use std::sync::{mpsc, Arc};

use rsnano_core::WorkRequest;

use super::Wallets;
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
            // TODO use callbacks, to make it async
            let root = request.root;
            let work = self.work_factory.generate_work(request);
            self.wallets.provide_work(&root, work);
        }
    }
}
