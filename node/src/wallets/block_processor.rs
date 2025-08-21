use std::sync::{mpsc, Arc};

use rsnano_core::Block;

use super::{Wallets, WalletsExt};
use crate::block_processing::{BlockProcessorQueue, BlockSource};

pub(crate) struct WalletBlockProcessor {
    inbound: mpsc::Receiver<Block>,
    wallets: Arc<Wallets>,
    block_processor: Arc<BlockProcessorQueue>,
}

impl WalletBlockProcessor {
    pub(crate) fn new(
        inbound: mpsc::Receiver<Block>,
        wallets: Arc<Wallets>,
        block_processor: Arc<BlockProcessorQueue>,
    ) -> Self {
        Self {
            inbound,
            wallets,
            block_processor,
        }
    }

    pub(crate) fn run(self) {
        while let Ok(block) = self.inbound.recv() {
            let hash = block.hash();

            // TODO use callbacks and make this async!
            let result = self
                .block_processor
                .push_blocking(block.into(), BlockSource::Local)
                .ok()
                .map(|r| r.ok())
                .flatten();

            self.wallets.block_processed(&hash, result);
        }
    }
}
