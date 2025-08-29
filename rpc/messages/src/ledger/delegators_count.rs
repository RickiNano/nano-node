use crate::{RpcCommand, common::AccountArg};
use rsnano_types::Account;

impl RpcCommand {
    pub fn delegators_count(account: Account) -> Self {
        Self::DelegatorsCount(AccountArg::new(account))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn serialize_delegators_count_command() {
        let command = RpcCommand::delegators_count(Account::ZERO);
        let serialized = serde_json::to_value(command).unwrap();
        let expected = json!({"action": "delegators_count", "account": "nano_1111111111111111111111111111111111111111111111111111hifc8npp"});
        assert_eq!(serialized, expected);
    }

    #[test]
    fn deserialize_delegators_count_command() {
        let json = r#"{"action": "delegators_count","account": "nano_1111111111111111111111111111111111111111111111111111hifc8npp"}"#;
        let deserialized: RpcCommand = serde_json::from_str(json).unwrap();
        let expected = RpcCommand::delegators_count(Account::ZERO);
        assert_eq!(deserialized, expected);
    }
}
