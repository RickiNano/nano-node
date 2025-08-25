use crate::command_handler::RpcCommandHandler;
use anyhow::anyhow;
use rsnano_rpc_messages::{BlockHashesResponse, WalletWithCountArgs};

impl RpcCommandHandler {
    pub(crate) fn wallet_republish(
        &self,
        _args: WalletWithCountArgs,
    ) -> anyhow::Result<BlockHashesResponse> {
        Err(anyhow!(
            "wallet_republish command isn't implemented in RsNano'"
        ))
    }
}
