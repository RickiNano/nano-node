use crate::{RpcCommand, common::KeyArg};
use rsnano_types::PublicKey;

impl RpcCommand {
    pub fn account_get(key: PublicKey) -> Self {
        Self::AccountGet(KeyArg::new(key))
    }
}

#[cfg(test)]
mod tests {
    use crate::RpcCommand;
    use rsnano_types::PublicKey;
    use serde_json::to_string_pretty;

    #[test]
    fn serialize_account_get_command() {
        assert_eq!(
            to_string_pretty(&RpcCommand::account_get(PublicKey::ZERO)).unwrap(),
            r#"{
  "action": "account_get",
  "key": "0000000000000000000000000000000000000000000000000000000000000000"
}"#
        )
    }

    #[test]
    fn deserialize_account_get_command() {
        let cmd = RpcCommand::account_get(PublicKey::ZERO);
        let serialized = serde_json::to_string_pretty(&cmd).unwrap();
        let deserialized: RpcCommand = serde_json::from_str(&serialized).unwrap();
        assert_eq!(cmd, deserialized)
    }
}
