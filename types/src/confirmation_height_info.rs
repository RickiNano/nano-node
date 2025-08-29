use crate::{BlockHash, DeserializationError, read_u64_ne};
use std::io::{Read, Write};

#[derive(Default, PartialEq, Eq, Debug, Clone)]
pub struct ConfirmationHeightInfo {
    pub height: u64,
    pub frontier: BlockHash,
}

impl ConfirmationHeightInfo {
    pub const SERIALIZED_SIZE: usize = BlockHash::SERIALIZED_SIZE + 8;

    pub fn new(height: u64, frontier: BlockHash) -> Self {
        Self { height, frontier }
    }

    pub fn new_test_instance() -> Self {
        Self {
            height: 42,
            frontier: BlockHash::from(7),
        }
    }

    pub fn to_bytes(&self) -> [u8; Self::SERIALIZED_SIZE] {
        let mut buffer = [0; Self::SERIALIZED_SIZE];
        self.serialize(&mut buffer.as_mut())
            .expect("Should serialize conf height info");
        buffer
    }

    fn serialize<T>(&self, writer: &mut T) -> std::io::Result<()>
    where
        T: Write,
    {
        writer.write_all(&self.height.to_ne_bytes())?;
        writer.write_all(self.frontier.as_bytes())
    }

    pub fn deserialize<T>(reader: &mut T) -> Result<Self, DeserializationError>
    where
        T: Read,
    {
        let height = read_u64_ne(reader)?;
        let frontier = BlockHash::deserialize(reader)?;
        Ok(Self { height, frontier })
    }
}
