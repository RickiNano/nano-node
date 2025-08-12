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
pub use unchecked_container::UncheckedMap;

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
    pub fn new(unchecked: Arc<Mutex<UncheckedMap>>, stats: Arc<Stats>) -> Self {
        let mutable = Arc::new(Mutex::new(UncheckedState::new()));
        let condition = Arc::new(Condvar::new());

        let thread = Arc::new(UncheckedMapLoop {
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

        for key in &delete_queue {
            unchecked.remove(key);
        }
    }
}
