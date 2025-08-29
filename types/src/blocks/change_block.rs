use super::{Block, BlockBase, BlockType};
use crate::{
    Account, Amount, Blake2HashBuilder, BlockHash, DependentBlocks, DeserializationError,
    JsonBlock, Link, PrivateKey, PublicKey, Root, Signature, WorkNonce, read_u64_le,
};
use std::io::Read;

#[derive(Clone, Debug)]
pub struct ChangeBlock {
    work: WorkNonce,
    signature: Signature,
    hashables: ChangeHashables,
    hash: BlockHash,
}

impl ChangeBlock {
    pub const SERIALIZED_SIZE: usize =
        ChangeHashables::SERIALIZED_SIZE + Signature::SERIALIZED_SIZE + std::mem::size_of::<u64>();

    pub fn new_test_instance() -> Self {
        let key = PrivateKey::from(42);
        ChangeBlockArgs {
            key: &key,
            previous: 123.into(),
            representative: 456.into(),
            work: 69420.into(),
        }
        .into()
    }

    pub fn mandatory_representative(&self) -> PublicKey {
        self.hashables.representative
    }

    pub fn deserialize<T>(reader: &mut T) -> Result<Self, DeserializationError>
    where
        T: Read,
    {
        let hashables = ChangeHashables {
            previous: BlockHash::deserialize(reader)?,
            representative: PublicKey::deserialize(reader)?,
        };

        let signature = Signature::deserialize(reader)?;
        let work = read_u64_le(reader)?;
        let hash = hashables.hash();
        Ok(Self {
            work: work.into(),
            signature,
            hashables,
            hash,
        })
    }

    pub fn dependent_blocks(&self) -> DependentBlocks {
        DependentBlocks::new(self.previous(), BlockHash::ZERO)
    }

    pub fn serialize_without_block_type<T>(&self, writer: &mut T) -> std::io::Result<()>
    where
        T: std::io::Write,
    {
        self.hashables.previous.serialize(writer)?;
        self.hashables.representative.serialize(writer)?;
        self.signature.serialize(writer)?;
        writer.write_all(&self.work.0.to_le_bytes())
    }
}

pub fn valid_change_block_predecessor(predecessor: BlockType) -> bool {
    matches!(
        predecessor,
        BlockType::LegacySend
            | BlockType::LegacyReceive
            | BlockType::LegacyOpen
            | BlockType::LegacyChange
    )
}

impl PartialEq for ChangeBlock {
    fn eq(&self, other: &Self) -> bool {
        self.work == other.work
            && self.signature == other.signature
            && self.hashables == other.hashables
    }
}

impl Eq for ChangeBlock {}

impl BlockBase for ChangeBlock {
    fn block_type(&self) -> BlockType {
        BlockType::LegacyChange
    }

    fn account_field(&self) -> Option<Account> {
        None
    }

    fn hash(&self) -> BlockHash {
        self.hash
    }

    fn link_field(&self) -> Option<Link> {
        None
    }

    fn signature(&self) -> &Signature {
        &self.signature
    }

    fn set_work(&mut self, work: WorkNonce) {
        self.work = work;
    }

    fn work(&self) -> WorkNonce {
        self.work
    }

    fn set_signature(&mut self, signature: Signature) {
        self.signature = signature.clone();
    }

    fn previous(&self) -> BlockHash {
        self.hashables.previous
    }

    fn root(&self) -> Root {
        self.previous().into()
    }

    fn balance_field(&self) -> Option<Amount> {
        None
    }

    fn source_field(&self) -> Option<BlockHash> {
        None
    }

    fn representative_field(&self) -> Option<PublicKey> {
        Some(self.hashables.representative)
    }

    fn valid_predecessor(&self, block_type: BlockType) -> bool {
        valid_change_block_predecessor(block_type)
    }

    fn destination_field(&self) -> Option<Account> {
        None
    }

    fn json_representation(&self) -> JsonBlock {
        JsonBlock::Change(JsonChangeBlock {
            previous: self.hashables.previous,
            representative: self.hashables.representative.into(),
            work: self.work,
            signature: self.signature.clone(),
        })
    }
}

#[derive(Clone, PartialEq, Eq, Debug)]
struct ChangeHashables {
    previous: BlockHash,
    representative: PublicKey,
}

impl ChangeHashables {
    const SERIALIZED_SIZE: usize = BlockHash::SERIALIZED_SIZE + Account::SERIALIZED_SIZE;

    fn hash(&self) -> BlockHash {
        Blake2HashBuilder::new()
            .update(self.previous.as_bytes())
            .update(self.representative.as_bytes())
            .build()
    }
}

pub struct ChangeBlockArgs<'a> {
    pub key: &'a PrivateKey,
    pub previous: BlockHash,
    pub representative: PublicKey,
    pub work: WorkNonce,
}

impl<'a> From<ChangeBlockArgs<'a>> for ChangeBlock {
    fn from(value: ChangeBlockArgs<'a>) -> Self {
        let hashables = ChangeHashables {
            previous: value.previous,
            representative: value.representative,
        };

        let hash = hashables.hash();
        let signature = value.key.sign(hash.as_bytes());

        Self {
            work: value.work,
            signature,
            hashables,
            hash,
        }
    }
}

impl<'a> From<ChangeBlockArgs<'a>> for Block {
    fn from(value: ChangeBlockArgs<'a>) -> Self {
        Block::LegacyChange(value.into())
    }
}

#[derive(PartialEq, Eq, Debug, serde::Serialize, serde::Deserialize, Clone)]
pub struct JsonChangeBlock {
    pub previous: BlockHash,
    pub representative: Account,
    pub signature: Signature,
    pub work: WorkNonce,
}

impl From<JsonChangeBlock> for ChangeBlock {
    fn from(value: JsonChangeBlock) -> Self {
        let hashables = ChangeHashables {
            previous: value.previous,
            representative: value.representative.into(),
        };

        let hash = hashables.hash();

        Self {
            work: value.work,
            signature: value.signature,
            hashables,
            hash,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Block, PrivateKey};

    #[test]
    fn create_block() {
        let key1 = PrivateKey::new();
        let previous = BlockHash::from(1);
        let block: ChangeBlock = ChangeBlockArgs {
            key: &key1,
            previous,
            representative: 2.into(),
            work: 5.into(),
        }
        .into();
        assert_eq!(block.previous(), previous);
        assert_eq!(block.root(), block.previous().into());
    }

    #[test]
    fn serialize() {
        let key1 = PrivateKey::new();
        let block1: ChangeBlock = ChangeBlockArgs {
            key: &key1,
            previous: 1.into(),
            representative: 2.into(),
            work: 5.into(),
        }
        .into();
        let mut buffer = Vec::new();
        block1.serialize_without_block_type(&mut buffer).unwrap();
        assert_eq!(ChangeBlock::SERIALIZED_SIZE, buffer.len());

        let block2 = ChangeBlock::deserialize(&mut buffer.as_slice()).unwrap();
        assert_eq!(block1, block2);
    }

    #[test]
    fn serialize_serde() {
        let block = Block::LegacyChange(ChangeBlock::new_test_instance());
        let serialized = serde_json::to_string_pretty(&block).unwrap();
        assert_eq!(
            serialized,
            r#"{
  "type": "change",
  "previous": "000000000000000000000000000000000000000000000000000000000000007B",
  "representative": "nano_11111111111111111111111111111111111111111111111111gahteczqci",
  "signature": "6F6E98FB9C3D0B91CBAF78C8613C7A7AE990AA627B9C1381D1D97AB7118C91D169381E3897A477286A4AFB68F7CD347F3FF16F8AB4C33241D8BF793CE29E730B",
  "work": "0000000000010F2C"
}"#
        );
    }
}
