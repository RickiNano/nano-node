use crate::{Aggregatable, MessageVariant};
use bitvec::prelude::BitArray;
use rsnano_types::{
    Account, Blake2Hash, Blake2HashBuilder, BlockHash, PrivateKey, PublicKey, Signature,
};
use rsnano_types::{DeserializationError, SnapshotNumber, read_u32_be};

pub type PreproposalHash = Blake2Hash;
pub type FrontiersHash = Blake2Hash;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Preproposal {
    pub snapshot_number: SnapshotNumber,
    pub frontiers: Vec<(Account, BlockHash)>,
    pub signer: PublicKey,
    pub signature: Signature,
}

fn sort_frontiers(frontiers: &mut Vec<(Account, BlockHash)>) {
    frontiers.sort_by(|(account_a, hash_a), (account_b, hash_b)| {
        let account_cmp = account_a.as_bytes().cmp(account_b.as_bytes());
        if account_cmp != std::cmp::Ordering::Equal {
            return account_cmp;
        }
        hash_a.as_bytes().cmp(hash_b.as_bytes())
    });
}

impl Preproposal {
    pub fn new(
        mut frontiers: Vec<(Account, BlockHash)>,
        private_key: &PrivateKey,
        snapshot_number: SnapshotNumber,
    ) -> Self {
        sort_frontiers(&mut frontiers);

        let mut preproposal = Self {
            snapshot_number,
            frontiers,
            signer: private_key.public_key(),
            signature: Signature::default(),
        };
        preproposal.signature = private_key.sign(preproposal.hash().as_bytes());

        preproposal
    }

    pub fn new_test_instance() -> Self {
        Self {
            snapshot_number: 1,
            frontiers: vec![(Account::from(1), BlockHash::from(100))],
            signer: PublicKey::from(2),
            signature: Signature::from_bytes([1; 64]),
        }
    }

    fn frontiers_hash(&self) -> FrontiersHash {
        let mut hash_builder = Blake2HashBuilder::default();
        for (account, hash) in self.frontiers.iter() {
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
        writer.write_all(&self.snapshot_number.to_be_bytes())?;
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
        let snapshot_number = read_u32_be(&mut bytes)?;
        let signer = PublicKey::deserialize(&mut bytes)?;
        let signature = Signature::deserialize(&mut bytes)?;
        let mut frontiers = Vec::new();

        while !bytes.is_empty() {
            let account = Account::deserialize(&mut bytes)?;
            let hash = BlockHash::deserialize(&mut bytes)?;
            frontiers.push((account, hash));
        }

        sort_frontiers(&mut frontiers);

        Ok(Preproposal {
            snapshot_number,
            frontiers,
            signer,
            signature,
        })
    }
}

impl MessageVariant for Preproposal {
    fn header_extensions(&self, payload_len: u16) -> BitArray<u16> {
        BitArray::new(payload_len)
    }
}

impl Aggregatable for Preproposal {
    fn signer(&self) -> PublicKey {
        self.signer
    }

    fn hash(&self) -> Blake2Hash {
        let frontiers_hash = self.frontiers_hash();
        let mut hash_builder: Blake2HashBuilder = Blake2HashBuilder::default();
        hash_builder = hash_builder
            .update(self.snapshot_number.to_be_bytes())
            .update(frontiers_hash.as_bytes())
            .update(self.signer.as_bytes());
        hash_builder.build()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Message, assert_deserializable};

    #[test]
    fn sign_new_preproposal() {
        let private_key = PrivateKey::from(42);
        let frontiers = vec![(Account::from(1), BlockHash::from(2))];

        let preproposal = Preproposal::new(frontiers, &private_key, 0);

        assert_eq!(preproposal.signer, private_key.public_key());

        let result = preproposal
            .signer
            .verify(preproposal.hash().as_bytes(), &preproposal.signature);
        assert_eq!(result, Ok(()));
    }

    #[test]
    fn hash_frontiers() {
        let key = PrivateKey::from(42);
        let preproposal1 = Preproposal::new(vec![], &key, 0);
        let preproposal2 = Preproposal::new(vec![], &key, 0);
        assert_eq!(preproposal1.frontiers_hash(), preproposal2.frontiers_hash());

        let preproposal1 = Preproposal::new(vec![(Account::from(1), BlockHash::from(2))], &key, 0);
        let preproposal2 = Preproposal::new(vec![(Account::from(1), BlockHash::from(2))], &key, 0);
        assert_eq!(preproposal1.frontiers_hash(), preproposal2.frontiers_hash());

        let preproposal1 = Preproposal::new(vec![(Account::from(1), BlockHash::from(2))], &key, 0);
        let preproposal2 =
            Preproposal::new(vec![(Account::from(10), BlockHash::from(20))], &key, 0);
        assert_ne!(preproposal1.frontiers_hash(), preproposal2.frontiers_hash());
    }

    #[test]
    fn preproposal_serialization() {
        let message = Message::SnapshotPreproposal(Preproposal::new_test_instance());
        assert_deserializable(&message);
    }

    #[test]
    fn hash_preproposals() {
        let key1 = PrivateKey::from(1);
        let key2 = PrivateKey::from(2);

        let snapshot_number1 = 0;
        let snapshot_number2 = 1;

        let frontiers1 = vec![
            (Account::from(1), BlockHash::from(10)),
            (Account::from(2), BlockHash::from(20)),
        ];
        let frontiers2 = vec![
            (Account::from(2), BlockHash::from(20)),
            (Account::from(1), BlockHash::from(10)),
        ];

        let p1 = Preproposal::new(frontiers1.clone(), &key1, snapshot_number1);
        let p2 = Preproposal::new(frontiers2.clone(), &key2, snapshot_number1);
        let p3 = Preproposal::new(frontiers1.clone(), &key1, snapshot_number2);
        let p4 = Preproposal::new(vec![], &key1, snapshot_number1);

        assert_ne!(p1.hash(), p2.hash());
        assert_ne!(p1.hash(), p3.hash());
        assert_ne!(p1.hash(), p4.hash());
    }
}
