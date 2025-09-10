use rsnano_ledger::RepWeights;
use rsnano_messages::{Preproposal, PreproposalHash};
use rsnano_types::Amount;
use std::collections::HashMap;

#[derive(Default)]
pub(super) struct PreproposalAggregator {
    preproposals: HashMap<PreproposalHash, Preproposal>,
    rep_weights: RepWeights,
    quorum_weight: Amount,
}

impl PreproposalAggregator {
    pub fn len(&self) -> usize {
        self.preproposals.len()
    }

    pub fn is_empty(&self) -> bool {
        self.preproposals.is_empty()
    }

    pub fn contains(&self, hash: &PreproposalHash) -> bool {
        self.preproposals.contains_key(hash)
    }

    pub fn add(&mut self, preproposal: Preproposal) {
        self.preproposals.insert(preproposal.hash(), preproposal);
    }

    fn set_rep_weights(&mut self, rep_weights: RepWeights, quorum_weight: Amount) {
        self.rep_weights = rep_weights;
        self.quorum_weight = quorum_weight;
    }

    fn vote_weight(&self, preproposal_hash: &PreproposalHash) -> Amount {
        let Some(preproposal) = self.preproposals.get(&preproposal_hash) else {
            return Amount::ZERO;
        };
        self.rep_weights.weight(&preproposal.signer)
    }

    fn has_quorum(&self) -> bool {
        let mut preproposals_weight = Amount::ZERO;
        for (_, preproposal) in &self.preproposals {
            preproposals_weight += self.rep_weights.weight(&preproposal.signer);
        }
        preproposals_weight >= self.quorum_weight 
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rsnano_ledger::RepWeights;
    use rsnano_types::{Account, Amount, BlockHash, PrivateKey};

    #[test]
    fn a_new_aggregator_is_empty() {
        let aggregator = PreproposalAggregator::default();
        assert_eq!(aggregator.len(), 0);
        assert!(aggregator.is_empty());
        assert_eq!(aggregator.contains(&PreproposalHash::from(123)), false);
    }

    #[test]
    fn add_preproposal() {
        let rep_key = PrivateKey::from(42);
        let weight = Amount::nano(500_000);
        let mut rep_weights = RepWeights::new();
        rep_weights.insert(rep_key.public_key(), weight);

        let mut aggregator = PreproposalAggregator::default();
        aggregator.set_rep_weights(rep_weights, Amount::MAX);

        let preproposal = Preproposal::new(vec![(Account::from(1), BlockHash::from(2))], &rep_key);
        aggregator.add(preproposal.clone());

        assert_eq!(aggregator.len(), 1);
        assert_eq!(aggregator.is_empty(), false);
        assert_eq!(aggregator.contains(&PreproposalHash::from(123)), false);
        assert_eq!(aggregator.contains(&preproposal.hash()), true);
        assert_eq!(aggregator.vote_weight(&preproposal.hash()), weight);
        assert_eq!(aggregator.has_quorum(), false);
    }

    #[test]
    fn reach_quorum() {
        let rep_key1 = PrivateKey::from(1);
        let rep_key2 = PrivateKey::from(2);

        let weight1 = Amount::nano(100_000);
        let weight2: Amount = Amount::nano(200_000);
        let mut rep_weights = RepWeights::new();
        rep_weights.insert(rep_key1.public_key(), weight1);
        rep_weights.insert(rep_key2.public_key(), weight2);

        let mut aggregator = PreproposalAggregator::default();
        aggregator.set_rep_weights(rep_weights, Amount::nano(300_000));

        let preproposal1 = Preproposal::new(vec![(Account::from(1), BlockHash::from(10))], &rep_key1);
        aggregator.add(preproposal1.clone());
        let preproposal2 = Preproposal::new(vec![(Account::from(2), BlockHash::from(20))], &rep_key2);
        aggregator.add(preproposal2.clone());

        assert_eq!(aggregator.has_quorum(), true);
    }
}
