use crate::RpcU64;
use crate::{RpcCommand, common::HashRpcMessage};
use rsnano_types::BlockHash;
use rsnano_types::JsonBlock;
use serde::{Deserialize, Serialize};

impl RpcCommand {
    pub fn unchecked_get(hash: BlockHash) -> Self {
        Self::UncheckedGet(HashRpcMessage::new(hash))
    }
}

#[derive(PartialEq, Eq, Debug, Serialize, Deserialize)]
pub struct UncheckedGetResponse {
    pub modified_timestamp: RpcU64,
    pub contents: JsonBlock,
}
