use rsnano_types::{Account, PublicKey, RawKey};
use serde::{Deserialize, Serialize};

#[derive(PartialEq, Eq, Debug, Serialize, Deserialize)]
pub struct KeyPairDto {
    pub private: RawKey,
    pub public: PublicKey,
    pub account: Account,
}

impl KeyPairDto {
    pub fn new(private: RawKey) -> Self {
        let public = PublicKey::from(private);
        Self {
            private,
            public,
            account: public.as_account(),
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::common::KeyPairDto;
    use rsnano_types::RawKey;

    #[test]
    fn serialize_keypair_dto() {
        let keypair = KeyPairDto::new(RawKey::from(1));
        let serialized = serde_json::to_string_pretty(&keypair).unwrap();
        assert_eq!(serialized, TEST_JSON);
    }

    #[test]
    fn deserialize_keypair_dto() {
        let deserialized: KeyPairDto = serde_json::from_str(TEST_JSON).unwrap();
        let expected = KeyPairDto::new(RawKey::from(1));
        assert_eq!(deserialized, expected);
    }

    const TEST_JSON: &'static str = r#"{
  "private": "0000000000000000000000000000000000000000000000000000000000000001",
  "public": "C969EC348895A49E21824E10E6B829EDEA50CCC26A83CE8986A3B95D12576058",
  "account": "nano_3kdbxitaj7f6mrir6miiwtw4muhcc58e6tn5st6rfaxsdnb7gr4roudwn951"
}"#;
}
