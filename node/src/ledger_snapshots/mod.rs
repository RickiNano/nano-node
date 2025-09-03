use rsnano_types::{Account, BlockHash};

pub struct LedgerSnapshots {}

impl LedgerSnapshots {
    pub fn new() -> Self {
        Self {}
    }

    pub fn collect_frontiers(&self) -> Vec<(Account, BlockHash)> {
        vec![(Account::from_bytes([1; 32]), BlockHash::from_bytes([2; 32]))]
    }
}
