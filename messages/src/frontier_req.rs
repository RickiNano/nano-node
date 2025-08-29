use super::MessageVariant;
use bitvec::prelude::BitArray;
use rsnano_types::{Account, DeserializationError};
use serde_derive::Serialize;
use std::{fmt::Display, io::Read};

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct FrontierReq {
    pub start: Account,
    pub age: u32,
    pub count: u32,
    pub only_confirmed: bool,
}

impl FrontierReq {
    pub const SERIALIZED_SIZE: usize =
        Account::SERIALIZED_SIZE
        + 4 // age
        + 4 //count
    ;

    pub fn new_test_instance() -> Self {
        Self {
            start: 1.into(),
            age: 2,
            count: 3,
            only_confirmed: false,
        }
    }

    pub const ONLY_CONFIRMED: usize = 1;

    pub fn serialize<T>(&self, writer: &mut T) -> std::io::Result<()>
    where
        T: std::io::Write,
    {
        self.start.serialize(writer)?;
        writer.write_all(&self.age.to_le_bytes())?;
        writer.write_all(&self.count.to_le_bytes())
    }

    pub fn deserialize(
        mut bytes: &[u8],
        extensions: BitArray<u16>,
    ) -> Result<Self, DeserializationError> {
        let start = Account::deserialize(&mut bytes)?;
        let mut buffer = [0u8; 4];
        bytes.read_exact(&mut buffer)?;
        let age = u32::from_le_bytes(buffer);
        bytes.read_exact(&mut buffer)?;
        let count = u32::from_le_bytes(buffer);
        let only_confirmed = extensions[FrontierReq::ONLY_CONFIRMED];

        Ok(FrontierReq {
            start,
            age,
            count,
            only_confirmed,
        })
    }
}

impl MessageVariant for FrontierReq {
    fn header_extensions(&self, _payload_len: u16) -> BitArray<u16> {
        let mut extensions = BitArray::default();
        extensions.set(Self::ONLY_CONFIRMED, self.only_confirmed);
        extensions
    }
}

impl Display for FrontierReq {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "\nstart={} maxage={} count={}",
            self.start, self.age, self.count
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Message, assert_deserializable};

    #[test]
    fn serialize() {
        let request = Message::FrontierReq(FrontierReq::new_test_instance());
        assert_deserializable(&request);
    }
}
