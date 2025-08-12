use std::collections::HashMap;

use rsnano_rpc_messages::{CountArgs, UncheckedResponse};

use crate::command_handler::RpcCommandHandler;

impl RpcCommandHandler {
    pub(crate) fn unchecked(&self, args: CountArgs) -> UncheckedResponse {
        let count = args.count.map(u64::from).unwrap_or(u64::MAX);
        let mut blocks = HashMap::new();

        let mut iterations = 0;
        self.node.unchecked_reenqueuer.for_each(
            |_key, block| {
                let json_block = block.json_representation();
                blocks.insert(block.hash(), json_block);
            },
            || {
                iterations += 1;
                iterations <= count
            },
        );

        UncheckedResponse::new(blocks)
    }
}
