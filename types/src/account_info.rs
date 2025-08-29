use num_traits::FromPrimitive;

use super::{BlockHash, Epoch};
use crate::{Amount, DeserializationError, PublicKey, UnixTimestamp, read_u8, read_u64_ne};
use std::io::Read;

/// Latest information about an account
#[derive(PartialEq, Eq, Clone, Default, Debug)]
pub struct AccountInfo {
    pub head: BlockHash,
    pub representative: PublicKey,
    pub open_block: BlockHash,
    pub balance: Amount,
    /** Seconds since posix epoch */
    pub modified: UnixTimestamp,
    pub block_count: u64,
    pub epoch: Epoch,
}

impl AccountInfo {
    pub const SERIALIZED_SIZE: usize = BlockHash::SERIALIZED_SIZE
        + PublicKey::SERIALIZED_SIZE // head
        + BlockHash::SERIALIZED_SIZE // representative
        + Amount::SERIALIZED_SIZE // balance
        + 8 // modified
        + 8 //block count
        + 1 // epoch
        ;

    pub fn new_test_instance() -> Self {
        Self {
            head: BlockHash::from(1),
            representative: PublicKey::from(2),
            open_block: BlockHash::from(3),
            balance: Amount::raw(42),
            modified: 4.into(),
            block_count: 5,
            epoch: Epoch::Epoch2,
        }
    }

    pub fn to_bytes(&self) -> [u8; Self::SERIALIZED_SIZE] {
        let mut buffer = [0; Self::SERIALIZED_SIZE];
        self.serialize(&mut buffer.as_mut())
            .expect("Should serialize account info");
        buffer
    }

    pub fn serialize<T>(&self, writer: &mut T) -> std::io::Result<()>
    where
        T: std::io::Write,
    {
        self.head.serialize(writer)?;
        self.representative.serialize(writer)?;
        self.open_block.serialize(writer)?;
        self.balance.serialize(writer)?;
        writer.write_all(&self.modified.as_u64().to_ne_bytes())?;
        writer.write_all(&self.block_count.to_ne_bytes())?;
        writer.write_all(&[self.epoch as u8])
    }

    pub fn deserialize<T>(reader: &mut T) -> Result<Self, DeserializationError>
    where
        T: Read,
    {
        let head = BlockHash::deserialize(reader)?;
        let representative = PublicKey::deserialize(reader)?;
        let open_block = BlockHash::deserialize(reader)?;
        let balance = Amount::deserialize(reader)?;
        let modified = read_u64_ne(reader)?.into();
        let block_count = read_u64_ne(reader)?;
        let epoch = Epoch::from_u8(read_u8(reader)?).ok_or(DeserializationError::InvalidData)?;
        Ok(Self {
            head,
            representative,
            open_block,
            balance,
            modified,
            block_count,
            epoch,
        })
    }
}
