mod unchecked_map;

use std::sync::{Arc, Mutex};

pub use unchecked_map::UncheckedMap;

/// Re-enqueues an unchecked block when its missing dependency block got inserted into the ledger
pub struct UncheckedBlockReenqueuer {}

impl UncheckedBlockReenqueuer {
    pub fn new(unchecked: Arc<Mutex<UncheckedMap>>) -> Self {
        Self {}
    }

    pub fn start(&self) {}

    pub fn stop(&self) {}
}
