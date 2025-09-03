use crate::command_handler::RpcCommandHandler;
use rsnano_types::{Account, BlockHash};

impl RpcCommandHandler {
    pub(crate) fn start_ledger_snapshot(&self) -> Vec<(Account, BlockHash)> {
        self.node.ledger_snapshots.collect_frontiers()
    }
}
