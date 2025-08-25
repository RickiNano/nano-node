use rsnano_rpc_messages::{BlockHashArgs, HashRpcMessage};
use rsnano_types::Block;

pub fn block_hash(args: BlockHashArgs) -> HashRpcMessage {
    let block_enum: Block = args.block.into();
    HashRpcMessage::new(block_enum.hash())
}
