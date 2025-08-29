use crate::{Account, Block, BlockHash, DeserializationError};
use primitive_types::U512;
use std::io::{Read, Write};

/// This struct represents the data written into the pending (receivable) database table key
/// the receiving account and hash of the send block identify a pending db table entry
#[derive(Default, PartialEq, Eq, Debug, Clone, Hash, PartialOrd, Ord, Copy)]
pub struct PendingKey {
    pub receiving_account: Account,
    pub send_block_hash: BlockHash,
}

impl PendingKey {
    pub const SERIALIZED_SIZE: usize = Account::SERIALIZED_SIZE + BlockHash::SERIALIZED_SIZE;

    pub fn new(receiving_account: Account, send_block_hash: BlockHash) -> Self {
        Self {
            receiving_account,
            send_block_hash,
        }
    }

    pub fn new_test_instance() -> Self {
        Self::new(Account::from(1), BlockHash::from(2))
    }

    pub fn to_bytes(&self) -> [u8; Self::SERIALIZED_SIZE] {
        let mut result = [0; Self::SERIALIZED_SIZE];
        self.serialize(&mut result.as_mut_slice()).unwrap();
        result
    }

    pub fn for_send_block(block: &Block) -> Self {
        Self::new(block.link_field().unwrap_or_default().into(), block.hash())
    }

    pub fn for_receive_block(block: &Block) -> Self {
        Self::new(
            block.account_field().unwrap(),
            block.link_field().unwrap_or_default().into(),
        )
    }

    pub fn serialize<T>(&self, writer: &mut T) -> std::io::Result<()>
    where
        T: Write,
    {
        self.receiving_account.serialize(writer)?;
        self.send_block_hash.serialize(writer)
    }

    pub fn deserialize<T>(reader: &mut T) -> Result<Self, DeserializationError>
    where
        T: Read,
    {
        let receiving_account = Account::deserialize(reader)?;
        let send_block_hash = BlockHash::deserialize(reader)?;
        Ok(Self {
            receiving_account,
            send_block_hash,
        })
    }
}

impl From<U512> for PendingKey {
    fn from(value: U512) -> Self {
        let buffer = value.to_big_endian();
        PendingKey::new(
            Account::from_slice(&buffer[..32]).unwrap(),
            BlockHash::from_slice(&buffer[32..]).unwrap(),
        )
    }
}

#[cfg(test)]
mod tests {
    use crate::PendingKey;

    #[test]
    fn pending_key_sorting() {
        let one = PendingKey::new(1.into(), 2.into());
        let one_same = PendingKey::new(1.into(), 2.into());
        let two = PendingKey::new(1.into(), 3.into());
        let three = PendingKey::new(2.into(), 1.into());
        assert!(one < two);
        assert!(one < three);
        assert!(two < three);
        assert!(one == one_same);
        assert!(one != two);
    }
}
