use rsnano_types::{PrivateKey, PublicKey, Signature};
use crate::ProposalHash;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ProposalVote {
    pub voter: PublicKey,
    pub signature: Signature,
    pub hash: ProposalHash,
}

impl ProposalVote {
    pub fn new(hash: ProposalHash, private_key: &PrivateKey) -> Self {
        let mut proposal_vote = Self {
            hash,
            voter: private_key.public_key(),
            signature: Signature::default(),
        };

        proposal_vote.signature = private_key.sign(hash.as_bytes());

        proposal_vote
    }

    pub fn new_test_instance() -> Self {
        Self {
            hash: ProposalHash::from(1),
            voter: PublicKey::from(2),
            signature: Signature::from_bytes([1; 64]),
        }
    }
}

mod tests {
    use rsnano_types::PrivateKey;
    use crate::{Aggregatable, Proposal, ProposalVote};

    #[test]
    fn sign_new_proposal_vote() {
        let private_key = PrivateKey::from(42);
        let proposal = Proposal::new_test_instance();

        let proposal_vote = ProposalVote::new(proposal.hash(), &private_key);

        assert_eq!(proposal_vote.voter, private_key.public_key());
        assert_eq!(proposal_vote.hash, proposal.hash());

        let result = proposal_vote
            .voter
            .verify(proposal_vote.hash.as_bytes(), &proposal_vote.signature);
        assert_eq!(result, Ok(()));
    }
}