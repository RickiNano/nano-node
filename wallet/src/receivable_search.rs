use std::sync::Arc;

use tracing::warn;

use super::Wallets;
use rsnano_utils::{CancellationToken, ticker::Tickable};

pub struct ReceivableSearch {
    wallets: Arc<Wallets>,
}

impl ReceivableSearch {
    pub fn new(wallets: Arc<Wallets>) -> Self {
        Self { wallets }
    }
}

impl Tickable for ReceivableSearch {
    fn tick(&mut self, _cancel_token: &CancellationToken) {
        // Reload wallets from disk
        self.wallets.reload();
        // Search pending
        if let Err(e) = self.wallets.search_receivable_all().wait() {
            warn!("Failed receivables search: {e:?}");
        }
    }
}
