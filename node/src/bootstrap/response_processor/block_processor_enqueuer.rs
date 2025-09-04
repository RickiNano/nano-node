use crate::{
    block_processing::{BlockContext, BlockProcessorQueue, BlockSource},
    bootstrap::response_processor::block_queue::NotifiableBlockQueue,
};
use rsnano_network::ChannelId;
use std::{sync::Arc, time::Duration};

pub(crate) struct BlockProcessorEnqueuer {
    block_queue: Arc<NotifiableBlockQueue>,
    block_processor_queue: Arc<BlockProcessorQueue>,
}

impl BlockProcessorEnqueuer {
    pub(crate) fn new(
        block_queue: Arc<NotifiableBlockQueue>,
        block_processor_queue: Arc<BlockProcessorQueue>,
    ) -> Self {
        Self {
            block_queue,
            block_processor_queue,
        }
    }

    pub fn run(&self) {
        while let Some(block) = self.block_queue.wait() {
            let hash = block.hash();

            let inserted = self.block_processor_queue.push(BlockContext::new(
                block,
                BlockSource::Bootstrap,
                // TODO use real channel id
                ChannelId::LOOPBACK,
            ));

            if inserted {
                self.block_queue.enqueued_for_processing(&hash);
            } else {
                // Give block processor some time to process the queued blocks...
                std::thread::sleep(Duration::from_millis(10));
            }
        }
    }
}
