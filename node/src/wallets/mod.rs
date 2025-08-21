pub(crate) mod block_processor;
mod config;
pub(crate) mod delayed_work_queue;
mod promises;
mod receivable_search;
mod wallet;
mod wallet_action_thread;
mod wallet_backup;
mod wallet_representatives;
mod wallets;
pub(crate) mod work;

use serde::{Deserialize, Serialize};

pub use config::{default_preconfigured_representatives_for_live, WalletsConfig};
pub use promises::{BlockPromise, MultiBlockPromise};
pub(crate) use receivable_search::*;
use std::fmt;
pub use wallet::*;
pub use wallet_action_thread::*;
pub(crate) use wallet_backup::*;
pub use wallet_representatives::*;
pub use wallets::*;

#[derive(FromPrimitive, Debug, Serialize, Deserialize, PartialEq, Eq, Clone)]
pub enum WalletsError {
    Generic,
    WalletNotFound,
    WalletLocked,
    AccountNotFound,
    InvalidPassword,
    BadPublicKey,
}

impl WalletsError {
    pub fn as_str(&self) -> &'static str {
        match self {
            WalletsError::Generic => "Unknown error",
            WalletsError::WalletNotFound => "Wallet not found",
            WalletsError::WalletLocked => "Wallet is locked",
            WalletsError::AccountNotFound => "Account not found",
            WalletsError::InvalidPassword => "Invalid password",
            WalletsError::BadPublicKey => "Bad public key",
        }
    }
}

impl fmt::Display for WalletsError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.as_str())
    }
}

impl std::error::Error for WalletsError {}
