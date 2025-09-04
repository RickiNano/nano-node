use rsnano_types::{Account, Block, BlockHash};
use rustc_hash::FxHashMap;
use std::{
    collections::VecDeque,
    sync::{Condvar, Mutex},
    time::Duration,
};

pub(crate) struct BlockQueue {
    max_accounts: usize,
    accounts: FxHashMap<Account, VecDeque<Block>>,
    blocks: usize,
}

impl BlockQueue {
    pub const DEFAULT_MAX_ACCOUNTS: usize = 1024 * 64;

    pub fn with_max_accounts(max_accounts: usize) -> Self {
        Self {
            max_accounts,
            accounts: Default::default(),
            blocks: 0,
        }
    }

    pub fn blocks(&self) -> usize {
        self.blocks
    }

    pub fn accounts(&self) -> usize {
        self.accounts.len()
    }

    pub fn is_empty(&self) -> bool {
        self.accounts() == 0
    }

    pub const fn max_accounts(&self) -> usize {
        self.max_accounts
    }

    pub fn insert(&mut self, account: Account, block: Block) -> bool {
        self.insert_multiple(account, [block])
    }

    pub fn insert_multiple(
        &mut self,
        account: Account,
        blocks: impl Into<VecDeque<Block>>,
    ) -> bool {
        if self.accounts.len() >= self.max_accounts() {
            return false;
        }

        let blocks = blocks.into();
        self.blocks += blocks.len();
        if let Some(old) = self.accounts.insert(account, blocks) {
            self.blocks -= old.len();
        }
        true
    }

    pub fn contains_account(&self, account: &Account) -> bool {
        self.accounts.contains_key(account)
    }

    pub fn next_to_process(&self) -> Option<&Block> {
        self.accounts.values().next()?.front()
    }

    pub fn enqueued_for_processing(&self, block_hash: &BlockHash) {
        // TODO
    }

    // TODO call
    pub fn processed(&mut self, block_hash: &BlockHash) {
        // TODO
    }

    // TODO call
    pub fn process_failed(&mut self, block_hash: &BlockHash) {
        // TODO
    }
}

impl Default for BlockQueue {
    fn default() -> Self {
        Self::with_max_accounts(Self::DEFAULT_MAX_ACCOUNTS)
    }
}

#[derive(Default)]
pub(crate) struct NotifiableBlockQueue {
    // queue + stopped
    queue: Mutex<(BlockQueue, bool)>,
    notify: Condvar,
}

impl NotifiableBlockQueue {
    pub fn insert_multiple(&self, account: Account, blocks: impl Into<VecDeque<Block>>) -> bool {
        let inserted = self
            .queue
            .lock()
            .unwrap()
            .0
            .insert_multiple(account, blocks);

        if inserted {
            self.notify.notify_one();
        }

        inserted
    }

    pub fn stop(&self) {
        self.queue.lock().unwrap().1 = true;
        self.notify.notify_all();
    }

    pub fn wait(&self) -> Option<Block> {
        let mut guard = self.queue.lock().unwrap();
        loop {
            guard = self
                .notify
                .wait_timeout_while(guard, Duration::from_secs(1), |(queue, stopped)| {
                    !*stopped && queue.next_to_process().is_some()
                })
                .unwrap()
                .0;

            if guard.1 {
                return None;
            }

            if let Some(block) = guard.0.next_to_process() {
                return Some(block.clone());
            }
        }
    }

    pub fn enqueued_for_processing(&self, block_hash: &BlockHash) {
        self.queue
            .lock()
            .unwrap()
            .0
            .enqueued_for_processing(block_hash)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn is_empty_after_creation() {
        let queue = BlockQueue::default();
        assert_eq!(queue.blocks(), 0, "blocks");
        assert_eq!(queue.accounts(), 0, "accounts");
        assert!(queue.is_empty(), "Should be empty");
        assert_eq!(
            queue.contains_account(&Account::from(1)),
            false,
            "Should not contain account"
        );
        assert!(
            queue.next_to_process().is_none(),
            "Should not have anything to process"
        );
    }

    #[test]
    fn has_default_max_accounts() {
        let queue = BlockQueue::default();
        assert_eq!(BlockQueue::DEFAULT_MAX_ACCOUNTS, 1024 * 64);
        assert_eq!(queue.max_accounts(), BlockQueue::DEFAULT_MAX_ACCOUNTS);
    }

    #[test]
    fn enqueues_a_block() {
        let mut queue = BlockQueue::default();
        let account = Account::from(1);
        let block = Block::new_test_instance();

        queue.insert(account, block.clone());

        assert_eq!(queue.accounts(), 1, "accounts");
        assert_eq!(queue.blocks(), 1, "blocks");
        assert!(queue.contains_account(&account));
        assert_eq!(queue.next_to_process().unwrap().hash(), block.hash());
    }

    #[test]
    fn enqueues_blocks_for_different_accounts() {
        let mut queue = BlockQueue::default();
        let account1 = Account::from(1);
        let account2 = Account::from(2);
        queue.insert(account1, Block::new_test_instance());
        queue.insert(account2, Block::new_test_instance());
        assert_eq!(queue.accounts(), 2, "accounts");
        assert_eq!(queue.blocks(), 2, "blocks");
        assert!(queue.contains_account(&account1), "Should contain account1");
        assert!(queue.contains_account(&account2), "Should contain account2");
    }

    #[test]
    fn replaces_previously_enqueued_blocks_for_same_account() {
        let mut queue = BlockQueue::default();
        let account = Account::from(1);
        let block1 = Block::new_test_instance_with_key(1);
        let block2 = Block::new_test_instance_with_key(2);
        queue.insert(account, block1);
        queue.insert(account, block2.clone());
        assert_eq!(queue.accounts(), 1, "accounts");
        assert_eq!(queue.blocks(), 1, "blocks");
        assert_eq!(queue.next_to_process().unwrap().hash(), block2.hash());
    }

    #[test]
    fn can_enqueue_multiple_blocks_for_one_account_at_once() {
        let mut queue = BlockQueue::default();
        let account = Account::from(1);
        let block1 = Block::new_test_instance_with_key(1);
        let block2 = Block::new_test_instance_with_key(2);

        queue.insert_multiple(account, [block1.clone(), block2]);

        assert_eq!(queue.accounts(), 1, "accounts");
        assert_eq!(queue.blocks(), 2, "blocks");
        assert_eq!(queue.next_to_process().unwrap().hash(), block1.hash());
    }

    #[test]
    fn max_accounts_is_configurable() {
        let queue = BlockQueue::with_max_accounts(3);
        assert_eq!(queue.max_accounts(), 3);
    }

    #[test]
    fn dont_enqueue_when_max_accounts_reached() {
        let mut queue = BlockQueue::with_max_accounts(3);
        let enqueued = queue.insert_multiple(
            Account::from(1),
            [
                Block::new_test_instance_with_key(10),
                Block::new_test_instance_with_key(20),
            ],
        );
        assert!(enqueued, "Should enqueue account1");

        let enqueued = queue.insert_multiple(
            Account::from(2),
            [
                Block::new_test_instance_with_key(30),
                Block::new_test_instance_with_key(40),
            ],
        );
        assert!(enqueued, "Should enqueue account2");

        let enqueued = queue.insert_multiple(
            Account::from(3),
            [
                Block::new_test_instance_with_key(50),
                Block::new_test_instance_with_key(60),
            ],
        );
        assert!(enqueued, "Should enqueue account3");

        let enqueued = queue.insert_multiple(
            Account::from(4),
            [
                Block::new_test_instance_with_key(70),
                Block::new_test_instance_with_key(80),
            ],
        );
        assert_eq!(enqueued, false, "Should NOT enqueue account4");

        assert_eq!(queue.accounts(), 3, "accounts");
        assert_eq!(queue.blocks(), 6, "blocks");
        assert!(
            queue.contains_account(&Account::from(1)),
            "Should contain account1"
        );
        assert!(
            queue.contains_account(&Account::from(2)),
            "Should contain account2"
        );
        assert!(
            queue.contains_account(&Account::from(3)),
            "Should contain account3"
        );
        assert_eq!(
            queue.contains_account(&Account::from(4)),
            false,
            "Should NOT contain account4"
        );
    }
}
