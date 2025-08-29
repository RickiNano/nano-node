use rsnano_types::{Account, PublicKey, RawKey};
use serde::{Deserialize, Serialize};

#[derive(PartialEq, Eq, Debug, Serialize, Deserialize)]
pub struct KeyPairDto {
    pub private: RawKey,
    pub public: PublicKey,
    pub account: Account,
}

impl KeyPairDto {
    pub fn new(private: RawKey, public: PublicKey, account: Account) -> Self {
        Self {
            private,
            public,
            account,
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::common::KeyPairDto;
    use rsnano_types::{Account, PublicKey, RawKey};

    #[test]
    fn serialize_keypair_dto() {
        let keypair = KeyPairDto::new(RawKey::ZERO, PublicKey::ZERO, Account::ZERO);

        let serialized = serde_json::to_string_pretty(&keypair).unwrap();

        assert_eq!(
            serialized,
            r#"{
  "private": "0000000000000000000000000000000000000000000000000000000000000000",
  "public": "0000000000000000000000000000000000000000000000000000000000000000",
  "account": "nano_1111111111111111111111111111111111111111111111111111hifc8npp"
}"#
        );
    }

    #[test]
    fn deserialize_keypair_dto() {
        let json_str = r#"{"private":"0000000000000000000000000000000000000000000000000000000000000000",
            "public":"0000000000000000000000000000000000000000000000000000000000000000",
            "account":"nano_1111111111111111111111111111111111111111111111111111hifc8npp"}"#;

        let deserialized: KeyPairDto = serde_json::from_str(json_str).unwrap();

        let expected = KeyPairDto::new(RawKey::ZERO, PublicKey::ZERO, Account::ZERO);

        assert_eq!(deserialized, expected);
    }
}
