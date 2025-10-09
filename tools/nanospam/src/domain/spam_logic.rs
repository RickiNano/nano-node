use crate::{
    domain::{AccountMap, BlockFactory, DelayedBlocks, SpamStrategy},
    high_prio_check::HighPrioTracker,
};

pub(crate) struct SpamLogic {
    pub(crate) delayed: DelayedBlocks,
    pub(crate) high_prio_tracker: HighPrioTracker,
    pub(crate) block_factory: BlockFactory,
}

impl SpamLogic {
    pub(crate) fn new(account_map: AccountMap, max_blocks: usize, strategy: SpamStrategy) -> Self {
        Self {
            delayed: Default::default(),
            high_prio_tracker: Default::default(),
            block_factory: BlockFactory::new(account_map, max_blocks, strategy),
        }
    }
}
