use rsnano_types::BlockHash;
use serde::{Deserialize, Serialize};

#[derive(PartialEq, Eq, Debug, Serialize, Deserialize)]
pub struct BlockDto {
    pub block: BlockHash,
}

impl BlockDto {
    pub fn new(block: BlockHash) -> Self {
        Self { block }
    }
}
