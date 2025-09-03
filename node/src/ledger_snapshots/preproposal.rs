use rsnano_types::{
    Account, Blake2Hash, Blake2HashBuilder, BlockHash, PrivateKey, PublicKey, Signature,
};

pub type PreProposalHash = Blake2Hash;
pub type FrontiersHash = Blake2Hash;

pub struct PreProposal {
    frontiers: Vec<(Account, BlockHash)>,
    signer: PublicKey,
    signature: Signature,
}

impl PreProposal {
    fn new(frontiers: Vec<(Account, BlockHash)>, private_key: &PrivateKey) -> PreProposal {
        todo!()
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
    fn new_preproposal() {}

    #[test]
    fn hash_frontiers() {
        assert_eq!(
            PreProposal::hash_frontiers(&[]),
            PreProposal::hash_frontiers(&[]),
        );

        assert_eq!(
            PreProposal::hash_frontiers(&[(Account::from(1), BlockHash::from(2))]),
            PreProposal::hash_frontiers(&[(Account::from(1), BlockHash::from(2))]),
        );

        assert_ne!(
            PreProposal::hash_frontiers(&[(Account::from(1), BlockHash::from(2))]),
            PreProposal::hash_frontiers(&[(Account::from(10), BlockHash::from(20))]),
        );
    }
}
