use crate::command_handler::RpcCommandHandler;
use anyhow::anyhow;
use rsnano_rpc_messages::{BlockHashesResponse, RepublishArgs};

impl RpcCommandHandler {
    pub(crate) fn republish(&self, _args: RepublishArgs) -> anyhow::Result<BlockHashesResponse> {
        Err(anyhow!("replubish command isn't implemented in RsNano'"))
    }
}
