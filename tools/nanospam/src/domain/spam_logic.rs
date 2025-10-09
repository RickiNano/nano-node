use crate::{domain::DelayedBlocks, high_prio_check::HighPrioTracker};

#[derive(Default)]
pub(crate) struct SpamLogic {
    pub(crate) delayed: DelayedBlocks,
    pub(crate) high_prio_tracker: HighPrioTracker,
}
