use anyhow::Ok;
use rsnano_rpc_messages::StartedResponse;
use crate::command_handler::RpcCommandHandler;

impl RpcCommandHandler {
    pub(crate) fn start_ledger_snapshot(&self) -> anyhow::Result<StartedResponse> {
        Ok(StartedResponse { started: true.into() })
    }
}