use anyhow::Result;
use num_traits::FromPrimitive;

use super::{BlockHash, Epoch};
use crate::{
    Amount, DeserializationError, PublicKey, UnixTimestamp, read_u8, read_u64_ne,
    stream::{Deserialize, Stream, StreamExt},
};
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
    pub fn to_bytes(&self) -> [u8; 129] {
        let mut buffer = [0; 129];
        self.serialize_writer(&mut buffer.as_mut())
            .expect("Should serialize account info");
        buffer
    }

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

    pub fn serialize_writer<T>(&self, writer: &mut T) -> std::io::Result<()>
    where
        T: std::io::Write,
    {
        self.head.serialize_writer(writer)?;
        self.representative.serialize_writer(writer)?;
        self.open_block.serialize_writer(writer)?;
        self.balance.serialize_writer(writer)?;
        writer.write_all(&self.modified.as_u64().to_ne_bytes())?;
        writer.write_all(&self.block_count.to_ne_bytes())?;
        writer.write_all(&[self.epoch as u8])
    }

    pub fn deserialize_reader<T>(reader: &mut T) -> Result<Self, DeserializationError>
    where
        T: Read,
    {
        let head = BlockHash::deserialize_reader(reader)?;
        let representative = PublicKey::deserialize_reader(reader)?;
        let open_block = BlockHash::deserialize_reader(reader)?;
        let balance = Amount::deserialize_reader(reader)?;
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

impl Deserialize for AccountInfo {
    type Target = Self;
    fn deserialize(stream: &mut dyn Stream) -> Result<AccountInfo> {
        Ok(Self {
            head: BlockHash::deserialize(stream)?,
            representative: PublicKey::deserialize(stream)?,
            open_block: BlockHash::deserialize(stream)?,
            balance: Amount::deserialize(stream)?,
            modified: stream.read_u64_ne()?.into(),
            block_count: stream.read_u64_ne()?,
            epoch: Epoch::from_u8(stream.read_u8()?).ok_or_else(|| anyhow!("invalid epoch"))?,
        })
    }
}
