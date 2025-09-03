use rsnano_types::{
    Account, Blake2Hash, Blake2HashBuilder, BlockHash, PrivateKey, PublicKey, Signature,
};

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
            signer: PublicKey::default(),
            signature: Signature::default(),
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
}

#[cfg(test)]
mod tests {
    use super::*;

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
}
