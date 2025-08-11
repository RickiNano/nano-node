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
    Block, BlockHash, HashOrAccount,
};
use rsnano_stats::{DetailType, StatType, Stats};
use unchecked_container::{Entry, UncheckedContainer};

/// Information on an unchecked block
#[derive(Clone, Debug)]
pub struct UncheckedInfo {
    pub block: Block,
}

impl UncheckedInfo {
    pub fn new(block: Block) -> Self {
        Self { block }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct UncheckedKey {
    pub previous: BlockHash,
    pub hash: BlockHash,
}

impl UncheckedKey {
    pub fn new(previous: BlockHash, hash: BlockHash) -> Self {
        Self { previous, hash }
    }
}

pub struct UncheckedMap {
    join_handle: Mutex<Option<JoinHandle<()>>>,
    thread: Arc<UncheckedMapLoop>,
    mutable: Arc<Mutex<UncheckedState>>,
    condition: Arc<Condvar>,
    stats: Arc<Stats>,
    max_unchecked_blocks: usize,
}

impl UncheckedMap {
    pub fn new(max_unchecked_blocks: usize, stats: Arc<Stats>, disable_delete: bool) -> Self {
        let mutable = Arc::new(Mutex::new(UncheckedState::new()));
        let condition = Arc::new(Condvar::new());

        let thread = Arc::new(UncheckedMapLoop {
            disable_delete,
            state: mutable.clone(),
            condition: condition.clone(),
            stats: stats.clone(),
            back_buffer: Mutex::new(VecDeque::new()),
        });

        Self {
            join_handle: Mutex::new(None),
            thread,
            mutable,
            condition,
            stats,
            max_unchecked_blocks,
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

    pub fn put(&self, dependency: HashOrAccount, block: Block) {
        let mut lock = self.mutable.lock().unwrap();
        let key = UncheckedKey::new(dependency.into(), block.hash());
        let inserted = lock
            .entries_container
            .insert(Entry::new(key, UncheckedInfo { block }));
        if lock.entries_container.len() > self.max_unchecked_blocks {
            lock.entries_container.pop_front();
        }
        if inserted {
            self.stats.inc(StatType::Unchecked, DetailType::Put);
        } else {
            self.stats.inc(StatType::Unchecked, DetailType::Duplicate);
        }
    }

    pub fn get(&self, hash: &HashOrAccount) -> Vec<Block> {
        let lock = self.mutable.lock().unwrap();
        let mut result = Vec::new();
        lock.entries_container.for_each_with_dependency(
            hash,
            |_, info| {
                result.push(info.block.clone());
            },
            || true,
        );
        result
    }

    pub fn clear(&self) {
        let mut lock = self.mutable.lock().unwrap();
        lock.entries_container.clear();
    }

    pub fn trigger(&self, dependency: &HashOrAccount) {
        let mut lock = self.mutable.lock().unwrap();
        lock.processed_queue.push_back(*dependency);
        drop(lock);
        self.stats.inc(StatType::Unchecked, DetailType::Trigger);
        self.condition.notify_all(); // Notify run ()
    }

    pub fn remove(&self, key: &UncheckedKey) {
        let mut lock = self.mutable.lock().unwrap();
        lock.entries_container.remove(key);
    }

    pub fn len(&self) -> usize {
        let lock = self.mutable.lock().unwrap();
        lock.entries_container.len()
    }

    pub fn is_empty(&self) -> bool {
        let lock = self.mutable.lock().unwrap();
        lock.entries_container.is_empty()
    }

    pub fn buffer_count(&self) -> usize {
        let lock = self.mutable.lock().unwrap();
        lock.processed_queue.len()
    }

    pub fn for_each(
        &self,
        action: impl FnMut(&UncheckedKey, &Block),
        predicate: impl FnMut() -> bool,
    ) {
        let lock = self.mutable.lock().unwrap();
        lock.entries_container.for_each(action, predicate)
    }

    pub fn for_each_with_dependency(
        &self,
        dependency: &HashOrAccount,
        action: impl FnMut(&UncheckedKey, &UncheckedInfo),
        predicate: impl FnMut() -> bool,
    ) {
        let lock = self.mutable.lock().unwrap();
        lock.entries_container
            .for_each_with_dependency(dependency, action, predicate)
    }

    pub fn set_satisfied_observer(&self, callback: Box<dyn Fn(&Block) + Send>) {
        self.mutable.lock().unwrap().satisfied_callback = Some(callback);
    }
}

impl ContainerInfoProvider for UncheckedMap {
    fn container_info(&self) -> ContainerInfo {
        [
            ("entries", self.len(), 0),
            ("queries", self.buffer_count(), 0),
        ]
        .into()
    }
}

impl Default for UncheckedMap {
    fn default() -> Self {
        Self::new(65536, Arc::new(Stats::default()), false)
    }
}

impl Drop for UncheckedMap {
    fn drop(&mut self) {
        debug_assert!(self.join_handle.lock().unwrap().is_none());
        self.stop()
    }
}

struct UncheckedState {
    stopped: bool,
    processed_queue: VecDeque<HashOrAccount>,
    entries_container: UncheckedContainer,
    satisfied_callback: Option<Box<dyn Fn(&Block) + Send>>,
}

impl UncheckedState {
    fn new() -> Self {
        Self {
            stopped: false,
            processed_queue: VecDeque::new(),
            entries_container: UncheckedContainer::new(),
            satisfied_callback: None,
        }
    }
}

pub struct UncheckedMapLoop {
    disable_delete: bool,
    state: Arc<Mutex<UncheckedState>>,
    condition: Arc<Condvar>,
    stats: Arc<Stats>,
    back_buffer: Mutex<VecDeque<HashOrAccount>>,
}

impl UncheckedMapLoop {
    fn run(&self) {
        let mut lock = self.state.lock().unwrap();
        while !lock.stopped {
            if !lock.processed_queue.is_empty() {
                let mut back_buffer_lock = self.back_buffer.lock().unwrap();
                std::mem::swap(&mut lock.processed_queue, back_buffer_lock.deref_mut());
                drop(lock);
                self.process_queries(&back_buffer_lock);
                lock = self.state.lock().unwrap();
                back_buffer_lock.clear();
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

    fn process_queries(&self, back_buffer: &VecDeque<HashOrAccount>) {
        for item in back_buffer {
            self.query_impl(item);
        }
    }

    pub fn query_impl(&self, hash: &HashOrAccount) {
        let mut delete_queue = Vec::new();
        let mut lock = self.state.lock().unwrap();
        lock.entries_container.for_each_with_dependency(
            hash,
            |key, info| {
                delete_queue.push(key.clone());
                self.stats.inc(StatType::Unchecked, DetailType::Satisfied);
                if let Some(callback) = &lock.satisfied_callback {
                    callback(&info.block);
                }
            },
            || true,
        );

        if !self.disable_delete {
            for key in &delete_queue {
                lock.entries_container.remove(key);
            }
        }
    }
}
