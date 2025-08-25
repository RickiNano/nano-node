use crate::{RpcCommand, common::HashRpcMessage};
use rsnano_types::BlockHash;

impl RpcCommand {
    pub fn work_cancel(hash: BlockHash) -> Self {
        Self::WorkCancel(HashRpcMessage::new(hash))
    }
}
