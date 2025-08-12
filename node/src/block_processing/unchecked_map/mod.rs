mod unchecked_container;

use std::{
    collections::VecDeque,
    ops::DerefMut,
    sync::{Arc, Condvar, Mutex},
    thread::JoinHandle,
    time::Duration,
};

use rsnano_core::{
    utils::{ContainerInfo, ContainerInfoProvider},
    Block, BlockHash,
};
use rsnano_stats::{DetailType, StatType, Stats};
use unchecked_container::UncheckedMap;

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct UncheckedKey {
    /// Hash of the unfulfilled dependency (corresponding send block or previous block)
    pub dependency_hash: BlockHash,
    /// Hash of the unchceked block
    pub unchecked_hash: BlockHash,
}

impl UncheckedKey {
    pub fn new(previous: BlockHash, hash: BlockHash) -> Self {
        Self {
            dependency_hash: previous,
            unchecked_hash: hash,
        }
    }
}

/// Re-enqueues an unchecked block when its missing dependency block got inserted into the ledger
pub struct UncheckedBlockReenqueuer {
    join_handle: Mutex<Option<JoinHandle<()>>>,
    thread: Arc<UncheckedMapLoop>,
    mutable: Arc<Mutex<UncheckedState>>,
    condition: Arc<Condvar>,
    stats: Arc<Stats>,
    unchecked: Arc<Mutex<UncheckedMap>>,
}

impl UncheckedBlockReenqueuer {
    pub fn new(max_blocks: usize, stats: Arc<Stats>, disable_delete: bool) -> Self {
        let mutable = Arc::new(Mutex::new(UncheckedState::new()));
        let condition = Arc::new(Condvar::new());

        let unchecked = Arc::new(Mutex::new(UncheckedMap::new(max_blocks, stats.clone())));
        let thread = Arc::new(UncheckedMapLoop {
            disable_delete,
            state: mutable.clone(),
            condition: condition.clone(),
            stats: stats.clone(),
            back_buffer: Mutex::new(VecDeque::new()),
            unchecked: unchecked.clone(),
        });

        Self {
            join_handle: Mutex::new(None),
            thread,
            mutable,
            condition,
            stats,
            unchecked,
        }
    }

    pub fn start(&self) {
        debug_assert!(self.join_handle.lock().unwrap().is_none());
        let thread_clone = Arc::clone(&self.thread);
        *self.join_handle.lock().unwrap() = Some(
            std::thread::Builder::new()
                .name("Unchecked".to_string())
                .spawn(move || {
                    thread_clone.run();
                })
                .unwrap(),
        );
    }

    pub fn stop(&self) {
        self.mutable.lock().unwrap().stopped = true;
        self.condition.notify_all();
        let handle = self.join_handle.lock().unwrap().take();
        if let Some(handle) = handle {
            handle.join().unwrap();
        }
    }

    pub fn put(&self, dependency: BlockHash, block: Block) {
        self.unchecked.lock().unwrap().put(dependency, block);
    }

    pub fn get(&self, hash: BlockHash) -> Vec<Block> {
        let unchecked = self.unchecked.lock().unwrap();
        let mut result = Vec::new();
        unchecked.for_each_with_dependency(
            hash,
            |_, block| {
                result.push(block.clone());
            },
            || true,
        );
        result
    }

    pub fn clear(&self) {
        self.unchecked.lock().unwrap().clear();
    }

    pub fn block_processed(&self, block_hash: BlockHash) {
        let mut lock = self.mutable.lock().unwrap();
        lock.processed_queue.push_back(block_hash);
        drop(lock);
        self.stats.inc(StatType::Unchecked, DetailType::Trigger);
        self.condition.notify_all(); // Notify run ()
    }

    pub fn remove(&self, key: &UncheckedKey) {
        self.unchecked.lock().unwrap().remove(key);
    }

    pub fn len(&self) -> usize {
        self.unchecked.lock().unwrap().len()
    }

    pub fn is_empty(&self) -> bool {
        self.unchecked.lock().unwrap().is_empty()
    }

    pub fn buffer_count(&self) -> usize {
        self.mutable.lock().unwrap().processed_queue.len()
    }

    pub fn for_each(
        &self,
        action: impl FnMut(&UncheckedKey, &Block),
        predicate: impl FnMut() -> bool,
    ) {
        self.unchecked.lock().unwrap().for_each(action, predicate)
    }

    pub fn for_each_with_dependency(
        &self,
        dependency: BlockHash,
        action: impl FnMut(&UncheckedKey, &Block),
        predicate: impl FnMut() -> bool,
    ) {
        self.unchecked
            .lock()
            .unwrap()
            .for_each_with_dependency(dependency, action, predicate)
    }

    pub fn set_satisfied_observer(&self, callback: Box<dyn Fn(&Block) + Send>) {
        self.mutable.lock().unwrap().satisfied_callback = Some(callback);
    }
}

impl ContainerInfoProvider for UncheckedBlockReenqueuer {
    fn container_info(&self) -> ContainerInfo {
        [
            ("entries", self.len(), 0),
            ("queries", self.buffer_count(), 0),
        ]
        .into()
    }
}

impl Default for UncheckedBlockReenqueuer {
    fn default() -> Self {
        Self::new(65536, Arc::new(Stats::default()), false)
    }
}

impl Drop for UncheckedBlockReenqueuer {
    fn drop(&mut self) {
        debug_assert!(self.join_handle.lock().unwrap().is_none());
        self.stop()
    }
}

struct UncheckedState {
    stopped: bool,
    processed_queue: VecDeque<BlockHash>,
    satisfied_callback: Option<Box<dyn Fn(&Block) + Send>>,
}

impl UncheckedState {
    fn new() -> Self {
        Self {
            stopped: false,
            processed_queue: VecDeque::new(),
            satisfied_callback: None,
        }
    }
}

pub struct UncheckedMapLoop {
    disable_delete: bool,
    state: Arc<Mutex<UncheckedState>>,
    condition: Arc<Condvar>,
    stats: Arc<Stats>,
    back_buffer: Mutex<VecDeque<BlockHash>>,
    unchecked: Arc<Mutex<UncheckedMap>>,
}

impl UncheckedMapLoop {
    fn run(&self) {
        let mut lock = self.state.lock().unwrap();
        while !lock.stopped {
            if !lock.processed_queue.is_empty() {
                let mut back_buffer_lock = self.back_buffer.lock().unwrap();
                std::mem::swap(&mut lock.processed_queue, back_buffer_lock.deref_mut());
                drop(lock);
                self.process_queries(&mut back_buffer_lock);
                lock = self.state.lock().unwrap();
            }

            lock = self
                .condition
                .wait_timeout_while(lock, Duration::from_secs(3), |other_lock| {
                    !other_lock.stopped && other_lock.processed_queue.is_empty()
                })
                .unwrap()
                .0;
        }
    }

    fn process_queries(&self, back_buffer: &mut VecDeque<BlockHash>) {
        for item in back_buffer.drain(..) {
            self.query_impl(item);
        }
    }

    pub fn query_impl(&self, hash: BlockHash) {
        let mut delete_queue = Vec::new();
        let mut unchecked = self.unchecked.lock().unwrap();
        unchecked.for_each_with_dependency(
            hash,
            |key, block| {
                delete_queue.push(key.clone());
                self.stats.inc(StatType::Unchecked, DetailType::Satisfied);
                if let Some(callback) = &self.state.lock().unwrap().satisfied_callback {
                    callback(&block);
                }
            },
            || true,
        );

        if !self.disable_delete {
            for key in &delete_queue {
                unchecked.remove(key);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rsnano_core::{Amount, PrivateKey, StateBlockArgs, DEV_GENESIS_KEY};
    use rsnano_ledger::{
        test_helpers::UnsavedBlockLatticeBuilder, DEV_GENESIS_ACCOUNT, DEV_GENESIS_PUB_KEY,
    };

    #[test]
    fn one_bootstrap() {
        let unchecked = UncheckedBlockReenqueuer::new(65536, Arc::new(Stats::default()), false);
        let mut lattice = UnsavedBlockLatticeBuilder::new();
        let block1 = lattice.genesis().send(&*DEV_GENESIS_KEY, 1);
        unchecked.put(block1.hash(), block1.clone());

        assert_eq!(unchecked.get(block1.hash()).len(), 1);

        let mut dependencies = Vec::new();
        unchecked.for_each(
            |key, _| {
                dependencies.push(key.unchecked_hash);
            },
            || true,
        );
        let hash1 = dependencies[0];
        assert_eq!(block1.hash(), hash1);
        let mut blocks = unchecked.get(hash1);
        assert_eq!(blocks.len(), 1);
        let block2 = blocks.remove(0);
        assert_eq!(block2.hash(), block1.hash());
    }

    // This test checks for basic operations in the unchecked table such as putting a new block, retrieving it, and
    // deleting it from the database
    #[test]
    fn simple() {
        let unchecked = UncheckedBlockReenqueuer::new(65536, Arc::new(Stats::default()), false);
        let mut lattice = UnsavedBlockLatticeBuilder::new();
        let block = lattice.genesis().send(&*DEV_GENESIS_KEY, 1);
        // Asserts the block wasn't added yet to the unchecked table
        let block_listing1 = unchecked.get(block.previous());
        assert!(block_listing1.is_empty());
        // Enqueues a block to be saved on the unchecked table
        unchecked.put(block.previous(), block.clone());
        // Retrieves the block from the database
        let block_listing2 = unchecked.get(block.previous());
        assert_ne!(block_listing2.len(), 0);
        // Asserts the added block is equal to the retrieved one
        assert_eq!(block_listing2[0].hash(), block.hash());
        // Deletes the block from the database
        unchecked.remove(&UncheckedKey::new(block.previous(), block.hash()));
        // Asserts the block is deleted
        let block_listing3 = unchecked.get(block.previous());
        assert!(block_listing3.is_empty());
    }

    // This test ensures the unchecked table is able to receive more than one block
    #[test]
    fn multiple() {
        let unchecked = UncheckedBlockReenqueuer::new(65536, Arc::new(Stats::default()), false);
        let mut lattice = UnsavedBlockLatticeBuilder::new();
        let block = lattice.genesis().send(&*DEV_GENESIS_KEY, 1);
        // Asserts the block wasn't added yet to the unchecked table
        let block_listing1 = unchecked.get(block.previous());
        assert!(block_listing1.is_empty());

        // Enqueues the first block
        unchecked.put(block.previous(), block.clone());
        // Enqueues a second block
        unchecked.put(6.into(), block.clone());
        // Waits for the block to get written in the database
        assert_eq!(unchecked.get(block.previous()).len(), 1);
        // Waits for and asserts the first block gets saved in the database
        assert!(unchecked.get(6.into()).len() > 0);
    }

    // This test ensures that a block can't occur twice in the unchecked table.
    #[test]
    fn double_put() {
        let unchecked = UncheckedBlockReenqueuer::new(65536, Arc::new(Stats::default()), false);
        let mut lattice = UnsavedBlockLatticeBuilder::new();
        let block = lattice.genesis().send(&*DEV_GENESIS_KEY, 1);
        // Asserts the block wasn't added yet to the unchecked table
        let block_listing1 = unchecked.get(block.previous());
        assert!(block_listing1.is_empty());

        // Enqueues the block to be saved in the unchecked table
        unchecked.put(block.previous(), block.clone());
        // Enqueues the block again in an attempt to have it there twice
        unchecked.put(block.previous(), block.clone());

        // Asserts the block was added at most once -- this is objective of this test.
        let block_listing2 = unchecked.get(block.previous());
        assert_eq!(block_listing2.len(), 1);
    }

    // Tests that recurrent get calls return the correct values
    #[test]
    fn multiple_get() {
        let unchecked = UncheckedBlockReenqueuer::new(65536, Arc::new(Stats::default()), false);
        // Instantiates three blocks
        let key1 = PrivateKey::new();
        let block1: Block = StateBlockArgs {
            key: &key1,
            previous: 1.into(),
            representative: *DEV_GENESIS_PUB_KEY,
            balance: Amount::raw(1),
            link: (*DEV_GENESIS_ACCOUNT).into(),
            work: 0.into(),
        }
        .into();

        let key2 = PrivateKey::new();
        let block2: Block = StateBlockArgs {
            key: &key2,
            previous: 2.into(),
            representative: *DEV_GENESIS_PUB_KEY,
            balance: Amount::raw(1),
            link: (*DEV_GENESIS_ACCOUNT).into(),
            work: 0.into(),
        }
        .into();

        let key3 = PrivateKey::new();
        let block3: Block = StateBlockArgs {
            key: &key3,
            previous: 3.into(),
            representative: *DEV_GENESIS_PUB_KEY,
            balance: Amount::raw(1),
            link: (*DEV_GENESIS_ACCOUNT).into(),
            work: 0.into(),
        }
        .into();
        // Add the blocks' info to the unchecked table
        unchecked.put(block1.previous(), block1.clone()); // unchecked1
        unchecked.put(block1.hash(), block1.clone()); // unchecked2
        unchecked.put(block2.previous(), block2.clone()); // unchecked3
        unchecked.put(block1.previous(), block2.clone()); // unchecked1
        unchecked.put(block1.hash(), block2.clone()); // unchecked2
        unchecked.put(block3.previous(), block3.clone());
        unchecked.put(block3.hash(), block3.clone()); // unchecked4
        unchecked.put(block1.previous(), block3.clone());
        // unchecked1

        let mut unchecked1 = Vec::new();
        // Asserts the entries will be found for the provided key
        let unchecked1_blocks = unchecked.get(block1.previous());
        assert_eq!(unchecked1_blocks.len(), 3);
        for i in unchecked1_blocks {
            unchecked1.push(i.hash());
        }
        // Asserts the payloads where correclty saved
        assert!(unchecked1.contains(&block1.hash()));
        assert!(unchecked1.contains(&block2.hash()));
        assert!(unchecked1.contains(&block3.hash()));
        let mut unchecked2 = Vec::new();
        // Asserts the entries will be found for the provided key
        let unchecked2_blocks = unchecked.get(block1.hash());
        assert_eq!(unchecked2_blocks.len(), 2);
        for i in unchecked2_blocks {
            unchecked2.push(i.hash());
        }
        // Asserts the payloads where correctly saved
        assert!(unchecked2.contains(&block1.hash()));
        assert!(unchecked2.contains(&block2.hash()));
        // Asserts the entry is found by the key and the payload is saved
        let unchecked3 = unchecked.get(block2.previous());
        assert_eq!(unchecked3.len(), 1);
        assert_eq!(unchecked3[0].hash(), block2.hash());
        // Asserts the entry is found by the key and the payload is saved
        let unchecked4 = unchecked.get(block3.hash());
        assert_eq!(unchecked4.len(), 1);
        assert_eq!(unchecked4[0].hash(), block3.hash());
        // Asserts no entry is found for a block that wasn't added
        let unchecked5 = unchecked.get(block2.hash());
        assert_eq!(unchecked5.len(), 0);
    }
}
