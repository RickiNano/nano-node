use rsnano_ledger::Ledger;
use rsnano_types::{Account, BlockHash};
use std::sync::Arc;

pub struct LedgerSnapshots {
    ledger: Arc<Ledger>,
}

impl LedgerSnapshots {
    pub fn new(ledger: Arc<Ledger>) -> Self {
        Self { ledger }
    }

    pub fn collect_frontiers(&self) -> Vec<(Account, BlockHash)> {
        vec![]
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rsnano_ledger::Ledger;
    use rsnano_types::AccountInfo;

    #[test]
    #[ignore]
    fn ledger_with_one_account() {
        let account = Account::from(1);
        let frontier = BlockHash::from(2);
        let account_info = AccountInfo {
            head: frontier,
            ..AccountInfo::new_test_instance()
        };
        let ledger = Arc::new(
            Ledger::new_null_builder()
                .account_info(&account, &account_info)
                .finish(),
        );
        let snapshots = LedgerSnapshots::new(ledger.clone());
        assert_eq!(snapshots.collect_frontiers(), [(account, frontier)]);
    }

    #[test]
    #[ignore]
    fn ledger_with_multiple_accounts() {
        let account1 = Account::from(1);
        let frontier1 = BlockHash::from(100);
        let account_info1 = AccountInfo {
            head: frontier1,
            ..AccountInfo::new_test_instance()
        };
        let account2 = Account::from(2);
        let frontier2 = BlockHash::from(200);
        let account_info2 = AccountInfo {
            head: frontier2,
            ..AccountInfo::new_test_instance()
        };

        let ledger = Arc::new(
            Ledger::new_null_builder()
                .account_info(&account1, &account_info1)
                .account_info(&account2, &account_info2)
                .finish(),
        );
        let snapshots = LedgerSnapshots::new(ledger.clone());
        assert_eq!(
            snapshots.collect_frontiers(),
            [(account1, frontier1), (account2, frontier2)]
        );
    }
}
