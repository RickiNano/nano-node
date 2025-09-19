use crate::{Aggregatable, MessageVariant, Preproposal, PreproposalHash};
use bitvec::array::BitArray;
use rsnano_types::{
    Blake2Hash, Blake2HashBuilder, DeserializationError, PrivateKey, PublicKey, Signature,
    SnapshotNumber, read_u32_be,
};

pub type PreproposalsHash = Blake2Hash;
pub type ProposalHash = Blake2Hash;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Proposal {
    pub snapshot_number: SnapshotNumber,
    // TODO: remove duplicates
    pub preproposal_hashes: Vec<PreproposalHash>,
    pub signer: PublicKey,
    pub signature: Signature,
}

impl Proposal {
    pub fn new<'a>(
        preproposals: impl IntoIterator<Item = &'a Preproposal>,
        private_key: &PrivateKey,
        snapshot_number: SnapshotNumber,
    ) -> Self {
        let mut preproposals: Vec<PreproposalHash> =
            preproposals.into_iter().map(|p| p.hash()).collect();
        preproposals.sort();

        let mut proposal = Self {
            snapshot_number,
            preproposal_hashes: preproposals,
            signer: private_key.public_key(),
            signature: Signature::default(),
        };

        proposal.signature = private_key.sign(proposal.hash().as_bytes());

        proposal
    }

    pub fn new_test_instance() -> Self {
        Self {
            snapshot_number: 1,
            preproposal_hashes: vec![PreproposalHash::from(1)],
            signer: PublicKey::from(2),
            signature: Signature::from_bytes([1; 64]),
        }
    }

    fn preproposals_hash(&self) -> PreproposalsHash {
        let mut hash_builder = Blake2HashBuilder::default();
        for hash in self.preproposal_hashes.iter() {
            hash_builder = hash_builder.update(hash.as_bytes());
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
        for preproposal in &self.preproposal_hashes {
            preproposal.serialize(writer)?;
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
        let mut preproposals = Vec::new();

        while !bytes.is_empty() {
            let preproposal = PreproposalHash::deserialize(&mut bytes)?;
            preproposals.push(preproposal);
        }
        preproposals.sort();

        Ok(Proposal {
            snapshot_number,
            preproposal_hashes: preproposals,
            signer,
            signature,
        })
    }
}

impl MessageVariant for Proposal {
    fn header_extensions(&self, payload_len: u16) -> BitArray<u16> {
        BitArray::new(payload_len)
    }
}

impl Aggregatable for Proposal {
    fn signer(&self) -> PublicKey {
        self.signer
    }

    fn hash(&self) -> Blake2Hash {
        let preproposals_hash = self.preproposals_hash();
        let mut hash_builder: Blake2HashBuilder = Blake2HashBuilder::default();
        hash_builder = hash_builder
            .update(self.snapshot_number.to_be_bytes())
            .update(preproposals_hash.as_bytes())
            .update(self.signer.as_bytes());
        hash_builder.build()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Message, Preproposal, assert_deserializable};
    use rsnano_types::{Account, BlockHash};

    #[test]
    fn hash_preproposals_is_order_invariant() {
        let pre1 = Preproposal::new(
            vec![
                (Account::from(1), BlockHash::from(10)),
                (Account::from(2), BlockHash::from(20)),
            ],
            &PrivateKey::new(),
            0,
        );
        let pre2 = Preproposal::new(
            vec![
                (Account::from(2), BlockHash::from(20)),
                (Account::from(1), BlockHash::from(10)),
            ],
            &PrivateKey::new(),
            0,
        );

        let p1 = Proposal::new(&[pre1.clone(), pre2.clone()], &PrivateKey::new(), 0);
        let p2 = Proposal::new(&[pre2, pre1], &PrivateKey::new(), 0);

        assert_eq!(p1.preproposals_hash(), p2.preproposals_hash());
    }

    #[test]
    fn sign_new_proposal() {
        let private_key = PrivateKey::from(42);
        let preproposals = [Preproposal::new_test_instance()];

        let proposal = Proposal::new(&preproposals, &private_key, 0);

        assert_eq!(proposal.signer, private_key.public_key());

        let result = proposal
            .signer
            .verify(proposal.hash().as_bytes(), &proposal.signature);
        assert_eq!(result, Ok(()));
    }

    #[test]
    fn proposal_serialization() {
        let message = Message::SnapshotProposal(Proposal::new_test_instance());
        assert_deserializable(&message);
    }

    #[test]
    fn hash_proposal() {
        let key1 = PrivateKey::from(1);
        let key2 = PrivateKey::from(2);
        let snapshot_number = 0;

        let p1 = Proposal::new(vec![], &key1, snapshot_number);
        let p2 = Proposal::new(&[Preproposal::new_test_instance()], &key1, snapshot_number);
        let p3 = Proposal::new(&[Preproposal::new_test_instance()], &key2, snapshot_number);
        let p4 = Proposal::new(
            &[Preproposal::new_test_instance()],
            &key2,
            snapshot_number + 1,
        );

        assert_ne!(p1.hash(), p2.hash());
        assert_ne!(p2.hash(), p3.hash());
        assert_ne!(p3.hash(), p4.hash());
    }
}
