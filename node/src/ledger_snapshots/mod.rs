pub mod preproposal;

use rsnano_ledger::Ledger;
use rsnano_types::{Account, BlockHash};
use std::sync::Arc;
use crate::ledger_snapshots::preproposal::PreProposal;
use rsnano_types::PrivateKey;

pub struct LedgerSnapshots {
    ledger: Arc<Ledger>,
}

impl LedgerSnapshots {
    pub fn new(ledger: Arc<Ledger>) -> Self {
        Self { ledger }
    }

    pub fn collect_frontiers(&self) -> Vec<(Account, BlockHash)> {
        self.ledger.confirmed().frontiers().collect()
    }

    pub fn create_preproposal(&self, private_key: &PrivateKey) -> PreProposal {
        let frontiers = self.collect_frontiers();

        PreProposal::new(frontiers, private_key)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rsnano_ledger::Ledger;
    use rsnano_types::{AccountInfo, ConfirmationHeightInfo};

    #[test]
    fn ledger_with_one_account() {
        let account = Account::from(1);
        let frontier = BlockHash::from(2);
        let ledger = Arc::new(
            Ledger::new_null_builder()
                .account_info(&account, &AccountInfo::new_test_instance())
                .confirmation_height(
                    &account,
                    &ConfirmationHeightInfo {
                        height: 0,
                        frontier,
                    },
                )
                .finish(),
        );
        let snapshots = LedgerSnapshots::new(ledger.clone());
        assert_eq!(snapshots.collect_frontiers(), [(account, frontier)]);
    }

    #[test]
    fn ledger_with_multiple_accounts() {
        let account1 = Account::from(1);
        let frontier1 = BlockHash::from(100);
        let account2 = Account::from(2);
        let frontier2 = BlockHash::from(200);

        let ledger = Arc::new(
            Ledger::new_null_builder()
                .account_info(&account1, &AccountInfo::new_test_instance())
                .account_info(&account2, &AccountInfo::new_test_instance())
                .confirmation_height(
                    &account1,
                    &ConfirmationHeightInfo {
                        height: 0,
                        frontier: frontier1,
                    },
                )
                .confirmation_height(
                    &account2,
                    &ConfirmationHeightInfo {
                        height: 0,
                        frontier: frontier2,
                    },
                )
                .finish(),
        );
        let snapshots = LedgerSnapshots::new(ledger.clone());
        assert_eq!(
            snapshots.collect_frontiers(),
            [(account1, frontier1), (account2, frontier2)]
        );
    }

    #[test]
    fn create_preproposal() {
        let account = Account::from(10);
        let frontier = BlockHash::from(2);
        let ledger = Arc::new(
            Ledger::new_null_builder()
                .account_info(&account, &AccountInfo::new_test_instance())
                .confirmation_height(
                    &account,
                    &ConfirmationHeightInfo {
                        height: 0,
                        frontier,
                    },
                )
                .finish(),
        );
        let snapshots = LedgerSnapshots::new(ledger.clone());

        let preproposal = snapshots.create_preproposal(&PrivateKey::from(1));

        assert!(preproposal.frontiers.contains(&(account, frontier)));
    }
}
