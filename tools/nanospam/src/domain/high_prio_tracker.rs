use rsnano_nullable_clock::Timestamp;
use rsnano_types::BlockHash;
use std::{
    collections::{HashMap, HashSet},
    time::Duration,
};

#[derive(Default)]
pub(crate) struct HighPrioTracker {
    enqueued: HashSet<BlockHash>,
    published: HashMap<BlockHash, Timestamp>,
}

impl HighPrioTracker {
    pub(crate) fn enqueued(&mut self, hash: BlockHash) {
        self.enqueued.insert(hash);
    }

    pub(crate) fn published(&mut self, hash: &BlockHash, now: Timestamp) -> bool {
        if self.enqueued.remove(hash) {
            self.published.insert(*hash, now);
            true
        } else {
            false
        }
    }

    pub(crate) fn confirmed(&mut self, hash: &BlockHash, now: Timestamp) -> Option<Duration> {
        if let Some(published) = self.published.remove(hash) {
            Some(published.elapsed(now))
        } else {
            None
        }
    }
}
