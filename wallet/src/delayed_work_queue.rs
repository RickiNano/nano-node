use std::collections::{BTreeSet, HashMap};

use rsnano_nullable_clock::Timestamp;
use rsnano_types::{Account, Root, WalletId};

#[derive(Default)]
pub(crate) struct DelayedWorkQueue {
    by_account: HashMap<Account, (Root, Timestamp, WalletId)>,
    by_date: BTreeSet<(Timestamp, Account)>,
}

impl DelayedWorkQueue {
    pub fn len(&self) -> usize {
        self.by_account.len()
    }

    pub fn insert(
        &mut self,
        wallet_id: WalletId,
        account: Account,
        root: Root,
        not_before: Timestamp,
    ) {
        if let Some((_, timestamp, _)) = self
            .by_account
            .insert(account, (root, not_before, wallet_id))
        {
            self.by_date.remove(&(timestamp, account));
        }
        self.by_date.insert((not_before, account));
    }

    pub fn remove(&mut self, account: &Account) {
        if let Some((_, timestamp, _)) = self.by_account.remove(account) {
            self.by_date.remove(&(timestamp, *account));
        }
    }

    pub fn pop(&mut self, now: Timestamp) -> Option<(WalletId, Account, Root)> {
        let (not_before, _) = self.by_date.first()?;
        if *not_before > now {
            return None;
        }

        let (_, account) = self.by_date.pop_first().unwrap();
        let (root, _, wallet_id) = self.by_account.remove(&account).unwrap();
        Some((wallet_id, account, root))
    }
}
