use crate::{RpcCommand, common::HashRpcMessage};
use rsnano_types::BlockHash;

impl RpcCommand {
    pub fn block_account(hash: BlockHash) -> Self {
        Self::BlockAccount(HashRpcMessage::new(hash))
    }
}

#[cfg(test)]
mod tests {
    use crate::RpcCommand;
    use rsnano_types::BlockHash;
    use serde_json::{from_str, to_string_pretty};

    #[test]
    fn serialize_account_block_count_command() {
        assert_eq!(
            serde_json::to_string_pretty(&RpcCommand::block_account(BlockHash::ZERO)).unwrap(),
            r#"{
  "action": "block_account",
  "hash": "0000000000000000000000000000000000000000000000000000000000000000"
}"#
        )
    }

    #[test]
    fn derialize_block_account_command() {
        let cmd = RpcCommand::block_account(BlockHash::ZERO);
        let serialized = to_string_pretty(&cmd).unwrap();
        let deserialized: RpcCommand = from_str(&serialized).unwrap();
        assert_eq!(cmd, deserialized)
    }
}
