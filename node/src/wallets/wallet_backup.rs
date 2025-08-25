use std::{path::PathBuf, sync::Arc, time::Duration};

use tracing::error;

use rsnano_utils::{ticker::Tickable, CancellationToken};
use rsnano_wallet::Wallets;

pub(crate) struct WalletBackup {
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

pub(crate) const BACKUP_INTERVAL: Duration = Duration::from_secs(60 * 5);
