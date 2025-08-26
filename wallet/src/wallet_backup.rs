use std::{path::PathBuf, sync::Arc};

use tracing::error;

use super::Wallets;
use rsnano_utils::{CancellationToken, ticker::Tickable};

pub struct WalletBackup {
    pub data_path: PathBuf,
    pub wallets: Arc<Wallets>,
}

impl Tickable for WalletBackup {
    fn tick(&mut self, _cancel_token: &CancellationToken) {
        let mut backup_path = self.data_path.clone();
        backup_path.push("backup");
        if let Err(e) = self.wallets.backup(&backup_path) {
            error!(error = ?e, "Could not create backup of wallets");
        }
    }
}
