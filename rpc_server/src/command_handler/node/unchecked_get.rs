use std::cell::RefCell;

use anyhow::anyhow;

use rsnano_node::block_processing::{UncheckedInfo, UncheckedKey};
use rsnano_rpc_messages::{HashRpcMessage, UncheckedGetResponse};

use crate::command_handler::RpcCommandHandler;

impl RpcCommandHandler {
    pub(crate) fn unchecked_get(
        &self,
        args: HashRpcMessage,
    ) -> anyhow::Result<UncheckedGetResponse> {
        let mut result = None;
        let done = RefCell::new(false);

        self.node.unchecked.for_each(
            |key: &UncheckedKey, info: &UncheckedInfo| {
                if key.hash == args.hash {
                    let modified_timestamp = info.modified;
                    let contents = info.block.json_representation();
                    result = Some(UncheckedGetResponse {
                        modified_timestamp: modified_timestamp.into(),
                        contents,
                    });
                    *done.borrow_mut() = true;
                }
            },
            || !*done.borrow(),
        );

        result.ok_or_else(|| anyhow!(Self::BLOCK_NOT_FOUND))
    }
}
