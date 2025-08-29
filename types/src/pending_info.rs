use crate::{Account, Amount, DeserializationError, Epoch, read_u8};
use num::FromPrimitive;
use std::io::{Read, Write};

/// Information on an uncollected send
/// This struct captures the data stored in a pending table entry
#[derive(PartialEq, Eq, Debug, Clone)]
pub struct PendingInfo {
    /// The account sending the funds
    pub source: Account,
    /// Amount receivable in this transaction
    pub amount: Amount,
    /// Epoch of sending block, this info is stored here to make it possible to prune the send block
    pub epoch: Epoch,
}

impl Default for PendingInfo {
    fn default() -> Self {
        Self {
            source: Default::default(),
            amount: Default::default(),
            epoch: Epoch::Epoch0,
        }
    }
}

impl PendingInfo {
    pub const SERIALIZED_SIZE: usize = Account::SERIALIZED_SIZE + Amount::SERIALIZED_SIZE + 1;

    pub fn new(source: Account, amount: Amount, epoch: Epoch) -> Self {
        Self {
            source,
            amount,
            epoch,
        }
    }

    pub fn new_test_instance() -> Self {
        Self::new(Account::from(3), Amount::raw(4), Epoch::Epoch2)
    }

    pub fn to_bytes(&self) -> [u8; 49] {
        let mut bytes = [0_u8; Self::SERIALIZED_SIZE];
        self.serialize(&mut bytes.as_mut_slice())
            .expect("Should serialize pending info");
        bytes
    }

    pub fn serialize<T>(&self, writer: &mut T) -> std::io::Result<()>
    where
        T: Write,
    {
        self.source.serialize(writer)?;
        self.amount.serialize(writer)?;
        writer.write_all(&[self.epoch as u8])
    }

    pub fn deserialize<T>(reader: &mut T) -> Result<Self, DeserializationError>
    where
        T: Read,
    {
        let source = Account::deserialize(reader)?;
        let amount = Amount::deserialize(reader)?;
        let epoch =
            FromPrimitive::from_u8(read_u8(reader)?).ok_or(DeserializationError::InvalidData)?;
        Ok(Self {
            source,
            amount,
            epoch,
        })
    }
}
