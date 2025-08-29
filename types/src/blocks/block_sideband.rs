use num::FromPrimitive;
use std::io::{Read, Write};

use super::Block;
use crate::{
    Account, Amount, BlockDetails, BlockType, DeserializationError, Epoch, UnixMillisTimestamp,
    read_u8,
};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BlockSideband {
    pub height: u64,
    pub timestamp: UnixMillisTimestamp,
    pub account: Account,
    pub balance: Amount,
    pub details: BlockDetails,
    pub source_epoch: Epoch,
}

impl BlockSideband {
    pub fn new_test_instance() -> Self {
        Self {
            height: 42,
            timestamp: UnixMillisTimestamp::new(1000),
            account: Account::from(1),
            balance: Amount::raw(42),
            details: BlockDetails {
                epoch: Epoch::Epoch2,
                is_send: true,
                is_receive: false,
                is_epoch: false,
            },
            source_epoch: Epoch::Epoch2,
        }
    }

    pub fn new_test_instance_for(block: &Block) -> Self {
        BlockSideband {
            height: 2,
            timestamp: 222222.into(),
            account: block.account_field().unwrap(),
            balance: block.balance_field().unwrap(),
            details: BlockDetails::new(Epoch::Epoch2, false, false, false),
            source_epoch: Epoch::Epoch0,
        }
    }

    pub fn serialized_size(block_type: BlockType) -> usize {
        let mut size = 0;

        if block_type != BlockType::State && block_type != BlockType::LegacyOpen {
            size += Account::SERIALIZED_SIZE; // account
        }

        if block_type != BlockType::LegacyOpen {
            size += 8; // height
        }

        if block_type == BlockType::LegacyReceive
            || block_type == BlockType::LegacyChange
            || block_type == BlockType::LegacyOpen
        {
            size += Amount::SERIALIZED_SIZE; // balance
        }

        size += 8; // timestamp

        if block_type == BlockType::State {
            // block_details must not be larger than the epoch enum
            const_assert!(std::mem::size_of::<Epoch>() == BlockDetails::serialized_size());
            size += BlockDetails::serialized_size() + std::mem::size_of::<Epoch>();
        }

        size
    }

    pub fn serialize<T>(&self, block_type: BlockType, writer: &mut T) -> std::io::Result<()>
    where
        T: Write,
    {
        if block_type != BlockType::State && block_type != BlockType::LegacyOpen {
            self.account.serialize(writer)?;
        }

        if block_type != BlockType::LegacyOpen {
            writer.write_all(&self.height.to_be_bytes())?;
        }

        if block_type == BlockType::LegacyReceive
            || block_type == BlockType::LegacyChange
            || block_type == BlockType::LegacyOpen
        {
            self.balance.serialize(writer)?;
        }

        writer.write_all(&self.timestamp.to_be_bytes())?;

        if block_type == BlockType::State {
            writer.write_all(&[self.details.packed(), self.source_epoch as u8])?;
        }
        Ok(())
    }

    pub fn deserialize<T>(
        reader: &mut T,
        block_type: BlockType,
    ) -> Result<Self, DeserializationError>
    where
        T: Read,
    {
        let account = if block_type != BlockType::State && block_type != BlockType::LegacyOpen {
            Account::deserialize(reader)?
        } else {
            Account::ZERO
        };

        let mut buffer = [0u8; 8];
        let height = if block_type != BlockType::LegacyOpen {
            reader.read_exact(&mut buffer)?;
            u64::from_be_bytes(buffer)
        } else {
            1
        };

        let balance = if block_type == BlockType::LegacyReceive
            || block_type == BlockType::LegacyChange
            || block_type == BlockType::LegacyOpen
        {
            Amount::deserialize(reader)?
        } else {
            Amount::ZERO
        };

        reader.read_exact(&mut buffer)?;
        let timestamp = UnixMillisTimestamp::from_be_bytes(buffer);

        let (details, source_epoch) = if block_type == BlockType::State {
            let details = BlockDetails::deserialize(reader)?;
            let source_epoch = FromPrimitive::from_u8(read_u8(reader)?)
                .ok_or(DeserializationError::InvalidData)?;
            (details, source_epoch)
        } else {
            let details = BlockDetails::new(Epoch::Epoch0, false, false, false);
            (details, Epoch::Epoch0)
        };

        Ok(Self {
            height,
            timestamp,
            account,
            balance,
            details,
            source_epoch,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn serialize() {
        let details = BlockDetails::new(Epoch::Epoch0, false, false, false);
        let sideband = BlockSideband {
            height: 4,
            timestamp: UnixMillisTimestamp::new(5),
            account: 1.into(),
            balance: 3.into(),
            details,
            source_epoch: Epoch::Epoch0,
        };
        let mut buffer = Vec::new();

        sideband
            .serialize(BlockType::LegacyReceive, &mut buffer)
            .unwrap();

        let deserialized =
            BlockSideband::deserialize(&mut buffer.as_slice(), BlockType::LegacyReceive).unwrap();
        assert_eq!(deserialized, sideband);
    }

    #[test]
    fn serialized_size() {
        assert_eq!(
            BlockSideband::serialized_size(BlockType::LegacySend),
            48,
            "legacy send"
        );
        assert_eq!(
            BlockSideband::serialized_size(BlockType::LegacyReceive),
            64,
            "legacy receive"
        );
        assert_eq!(
            BlockSideband::serialized_size(BlockType::LegacyOpen),
            24,
            "legacy open"
        );
        assert_eq!(
            BlockSideband::serialized_size(BlockType::LegacyChange),
            64,
            "legacy change"
        );
        assert_eq!(
            BlockSideband::serialized_size(BlockType::State),
            18,
            "legacy state"
        );
    }
}
