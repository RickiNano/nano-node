mod unchecked_container;

use std::{
    collections::VecDeque,
    ops::DerefMut,
    sync::{Arc, Condvar, Mutex},
    thread::JoinHandle,
    time::Duration,
};

use rsnano_core::{Block, BlockHash};
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
    pub fn new(dependency_hash: BlockHash, unchecked_hash: BlockHash) -> Self {
        Self {
            dependency_hash,
            unchecked_hash,
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

    pub fn block_processed(&self, block_hash: BlockHash) {
        let mut lock = self.mutable.lock().unwrap();
        lock.processed_queue.push_back(block_hash);
        drop(lock);
        self.stats.inc(StatType::Unchecked, DetailType::Trigger);
        self.condition.notify_all(); // Notify run ()
    }

    pub fn set_satisfied_observer(&self, callback: Box<dyn Fn(&Block) + Send>) {
        self.mutable.lock().unwrap().satisfied_callback = Some(callback);
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

    pub fn query_impl(&self, processed_hash: BlockHash) {
        let mut satisfied_blocks = Vec::new();
        let mut unchecked = self.unchecked.lock().unwrap();
        for block in unchecked.blocks_dependend_on(processed_hash) {
            satisfied_blocks.push((processed_hash, block.clone()));
        }

        for (dependency_hash, unchecked_block) in satisfied_blocks {
            self.stats.inc(StatType::Unchecked, DetailType::Satisfied);
            if let Some(callback) = &self.state.lock().unwrap().satisfied_callback {
                callback(&unchecked_block);
            }
            unchecked.remove(&UncheckedKey::new(dependency_hash, unchecked_block.hash()));
        }
    }
}
