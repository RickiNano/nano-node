use std::sync::{mpsc, Arc};

use super::{DelayedWorkRequest, Wallets};

pub(crate) struct WalletWorkProvider {
    wallets: Arc<Wallets>,
    queue: mpsc::Receiver<DelayedWorkRequest>,
}

impl WalletWorkProvider {
    pub(crate) fn new(wallets: Arc<Wallets>, queue: mpsc::Receiver<DelayedWorkRequest>) -> Self {
        Self { wallets, queue }
    }

    pub fn run(mut self) {
        while let Ok(request) = self.queue.recv() {
            // TODO
        }
    }
}
