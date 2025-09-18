use std::collections::HashMap;

use crate::representatives::ConsensusParams;
use rsnano_messages::{ProposalHash, ProposalVote};
use rsnano_types::Amount;

pub(crate) fn find_winner_proposal<'a>(
    params: &ConsensusParams,
    votes: impl IntoIterator<Item = &'a ProposalVote>,
) -> Option<ProposalHash> {
    let mut tallies: HashMap<ProposalHash, Amount> = HashMap::new();

    for vote in votes {
        let weight = tallies.entry(vote.proposal_hash).or_default();
        *weight += params.rep_weights.weight(&vote.voter);
    }

    tallies
        .into_iter()
        .find(|(p, w)| *w >= params.quorum_weight)
        .map(|(p, w)| p)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ledger_snapshots::Aggregator;
    use rsnano_ledger::RepWeights;
    use rsnano_messages::Preproposal;
    use rsnano_types::{Account, BlockHash, PrivateKey};

    #[test]
    fn default_quorum_weight_is_max() {
        let params = ConsensusParams::default();
        assert_eq!(params.quorum_weight, Amount::MAX);
    }

    #[test]
    fn a_winner_proposal_is_not_found_if_there_are_no_votes() {
        assert_eq!(
            find_winner_proposal(&ConsensusParams::default(), vec![]),
            None
        );
    }

    #[test]
    fn a_winner_proposal_is_not_found_if_quorum_is_not_reached() {
        let mut params = ConsensusParams::default();
        let rep_key = PrivateKey::from(1);
        let weight = Amount::nano(100_000);
        let mut rep_weights = RepWeights::new();
        rep_weights.insert(rep_key.public_key(), weight);
        params.set_rep_weights(rep_weights, Amount::MAX);

        let proposal_hash = ProposalHash::from(1);
        let proposal_vote = ProposalVote::new(proposal_hash, &rep_key, 0);

        assert_eq!(find_winner_proposal(&params, &[proposal_vote]), None);
    }

    #[test]
    fn a_winner_proposal_is_found_if_quorum_is_reached() {
        let mut params = ConsensusParams::default();

        let rep_key1 = PrivateKey::from(1);
        let rep_key2 = PrivateKey::from(2);
        let weight = Amount::nano(100_000);

        let mut rep_weights = RepWeights::new();
        rep_weights.insert(rep_key1.public_key(), weight);
        rep_weights.insert(rep_key2.public_key(), weight);
        params.set_rep_weights(rep_weights, weight * 2);

        let proposal_hash = ProposalHash::from(1);
        let proposal_vote1 = ProposalVote::new(proposal_hash, &rep_key1, 0);
        let proposal_vote2 = ProposalVote::new(proposal_hash, &rep_key2, 0);

        assert_eq!(
            find_winner_proposal(&params, &[proposal_vote1, proposal_vote2]),
            Some(proposal_hash)
        );
    }
}
