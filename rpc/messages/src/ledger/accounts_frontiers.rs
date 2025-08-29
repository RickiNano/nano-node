use crate::{RpcCommand, common::AccountsRpcMessage};
use rsnano_types::Account;

impl RpcCommand {
    pub fn accounts_frontiers(accounts: Vec<Account>) -> Self {
        Self::AccountsFrontiers(AccountsRpcMessage::new(accounts))
    }
}

#[cfg(test)]
mod tests {
    use crate::RpcCommand;
    use rsnano_types::Account;
    use serde_json::to_string_pretty;

    #[test]
    fn serialize_accounts_frontiers_command() {
        assert_eq!(
            to_string_pretty(&RpcCommand::accounts_frontiers(vec![Account::ZERO])).unwrap(),
            r#"{
  "action": "accounts_frontiers",
  "accounts": [
    "nano_1111111111111111111111111111111111111111111111111111hifc8npp"
  ]
}"#
        )
    }

    #[test]
    fn deserialize_accounts_frontiers_command() {
        let json_str = r#"{
    "action": "accounts_frontiers",
    "accounts": ["nano_1111111111111111111111111111111111111111111111111111hifc8npp"]
    }"#;
        let deserialized: RpcCommand = serde_json::from_str(json_str).unwrap();
        let expected_command = RpcCommand::accounts_frontiers(vec![Account::ZERO]);
        assert_eq!(deserialized, expected_command);
    }
}
