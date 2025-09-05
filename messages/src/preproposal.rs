use rsnano_types::{
    Account, Blake2Hash, Blake2HashBuilder, BlockHash, PrivateKey, PublicKey, Signature,
};
use rsnano_types::DeserializationError;
use bitvec::prelude::BitArray;
use crate::MessageVariant;

pub type PreProposalHash = Blake2Hash;
pub type FrontiersHash = Blake2Hash;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Preproposal {
    pub frontiers: Vec<(Account, BlockHash)>,
    pub signer: PublicKey,
    pub signature: Signature,
}

impl Preproposal {
    pub fn new(frontiers: Vec<(Account, BlockHash)>, private_key: &PrivateKey) -> Self {
        let frontiers_hash = Preproposal::hash_frontiers(&frontiers);
        let signature = private_key.sign(frontiers_hash.as_bytes());

        Self {
            frontiers,
            signer: private_key.public_key(),
            signature,
        }
    }

    pub fn new_test_instance() -> Self {
        Self {
            frontiers: vec![(Account::from(1), BlockHash::from(100))],
            signer: PublicKey::from(2),
            signature: Signature::from_bytes([1; 64]),
        }
    }

    fn hash_frontiers(frontiers: &[(Account, BlockHash)]) -> FrontiersHash {
        let mut hash_builder = Blake2HashBuilder::default();
        for (account, hash) in frontiers {
            hash_builder = hash_builder
                .update(account.as_bytes())
                .update(hash.as_bytes());
        }
        hash_builder.build()
    }

    pub fn serialize<T>(&self, writer: &mut T) -> std::io::Result<()>
    where
        T: std::io::Write,
    {
        self.signer.serialize(writer)?;
        self.signature.serialize(writer)?;
        for (account, hash) in &self.frontiers {
            account.serialize(writer)?;
            hash.serialize(writer)?;
        }
        Ok(())
    }

    pub const fn serialized_size(extensions: BitArray<u16>) -> usize {
        extensions.data as usize
    }

    pub fn deserialize(mut bytes: &[u8]) -> Result<Self, DeserializationError> {
        let signer = PublicKey::deserialize(&mut bytes)?;
        let signature = Signature::deserialize(&mut bytes)?;
        let mut frontiers = Vec::new();

        while !bytes.is_empty() {
            let account = Account::deserialize(&mut bytes)?;
            let hash = BlockHash::deserialize(&mut bytes)?;
            frontiers.push((account, hash));
        }

        Ok(Preproposal { frontiers, signer, signature })
    }
}

impl MessageVariant for Preproposal {
    fn header_extensions(&self, payload_len: u16) -> BitArray<u16> {
        BitArray::new(payload_len)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{assert_deserializable, Message};

    #[test]
    fn sign_new_preproposal() {
        let private_key = PrivateKey::from(42);
        let frontiers = vec![(Account::from(1), BlockHash::from(2))];

        let preproposal = Preproposal::new(frontiers, &private_key);

        assert_eq!(preproposal.signer, private_key.public_key());

        let frontiers_hash = Preproposal::hash_frontiers(&preproposal.frontiers);
        let result = preproposal
            .signer
            .verify(frontiers_hash.as_bytes(), &preproposal.signature);
        assert_eq!(result, Ok(()));
    }

    #[test]
    fn hash_frontiers() {
        assert_eq!(
            Preproposal::hash_frontiers(&[]),
            Preproposal::hash_frontiers(&[]),
        );

        assert_eq!(
            Preproposal::hash_frontiers(&[(Account::from(1), BlockHash::from(2))]),
            Preproposal::hash_frontiers(&[(Account::from(1), BlockHash::from(2))]),
        );

        assert_ne!(
            Preproposal::hash_frontiers(&[(Account::from(1), BlockHash::from(2))]),
            Preproposal::hash_frontiers(&[(Account::from(10), BlockHash::from(20))]),
        );
    }

    #[test]
    fn preproposal_serialization() {
        let message = Message::SnapshotPreproposal(Preproposal::new_test_instance());
        assert_deserializable(&message);
    }
}
