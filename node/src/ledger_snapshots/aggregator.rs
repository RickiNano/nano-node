use rsnano_ledger::RepWeights;
use rsnano_messages::{Aggregatable, PreproposalHash};
use rsnano_types::{Amount, Blake2Hash, PublicKey};
use std::collections::{HashMap, HashSet};

pub(super) struct Aggregator<T: Aggregatable> {
    values: HashMap<Blake2Hash, T>,
    signers: HashSet<PublicKey>,
    pub(crate) rep_weights: RepWeights,
    pub(crate) quorum_weight: Amount,
}

impl<T: Aggregatable> Default for Aggregator<T> {
    fn default() -> Self {
        Self {
            values: Default::default(),
            signers: Default::default(),
            rep_weights: Default::default(),
            quorum_weight: Amount::MAX,
        }
    }
}

impl<T: Aggregatable> Aggregator<T> {
    pub fn len(&self) -> usize {
        self.values.len()
    }

    pub fn is_empty(&self) -> bool {
        self.values.is_empty()
    }

    pub fn contains(&self, hash: &PreproposalHash) -> bool {
        self.values.contains_key(hash)
    }

    pub fn add(&mut self, value: T) {
        if self.signers.insert(value.signer()) {
            self.values.insert(value.hash(), value);
        }
    }

    pub(crate) fn set_rep_weights(&mut self, rep_weights: RepWeights, quorum_weight: Amount) {
        self.rep_weights = rep_weights;
        self.quorum_weight = quorum_weight;
    }

    fn vote_weight(&self, preproposal_hash: &PreproposalHash) -> Amount {
        let Some(preproposal) = self.values.get(&preproposal_hash) else {
            return Amount::ZERO;
        };
        self.rep_weights.weight(&preproposal.signer())
    }

    pub(crate) fn has_quorum(&self) -> bool {
        let mut preproposals_weight = Amount::ZERO;
        for (_, preproposal) in &self.values {
            preproposals_weight += self.rep_weights.weight(&preproposal.signer());
        }
        preproposals_weight >= self.quorum_weight
    }

    pub(crate) fn values(&self) -> impl Iterator<Item = &T> {
        self.values.values()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rsnano_ledger::RepWeights;
    use rsnano_messages::Preproposal;
    use rsnano_types::{Account, Amount, BlockHash, PrivateKey};

    #[test]
    fn a_new_aggregator_is_empty() {
        let aggregator = Aggregator::<Preproposal>::default();
        assert_eq!(aggregator.len(), 0);
        assert!(aggregator.is_empty());
        assert_eq!(aggregator.contains(&PreproposalHash::from(123)), false);
        assert_eq!(aggregator.quorum_weight, Amount::MAX);
    }

    #[test]
    fn add_preproposal() {
        let rep_key = PrivateKey::from(42);
        let weight = Amount::nano(500_000);
        let mut rep_weights = RepWeights::new();
        rep_weights.insert(rep_key.public_key(), weight);

        let mut aggregator = Aggregator::default();
        aggregator.set_rep_weights(rep_weights, Amount::MAX);

        let preproposal = Preproposal::new(test_frontiers(), &rep_key);
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

        let mut rep_weights = RepWeights::new();
        rep_weights.insert(rep_key1.public_key(), Amount::nano(100_000));
        rep_weights.insert(rep_key2.public_key(), Amount::nano(200_000));

        let mut aggregator = Aggregator::default();
        aggregator.set_rep_weights(rep_weights, Amount::nano(300_000));

        let preproposal1 = Preproposal::new(test_frontiers(), &rep_key1);
        aggregator.add(preproposal1.clone());
        let preproposal2 = Preproposal::new(test_frontiers(), &rep_key2);
        aggregator.add(preproposal2.clone());

        assert_eq!(aggregator.has_quorum(), true);
    }

    #[test]
    fn only_allow_one_preproposal_per_signer() {
        let rep_key = PrivateKey::from(1);
        let mut rep_weights = RepWeights::new();
        rep_weights.insert(rep_key.public_key(), Amount::nano(100_000));

        let mut aggregator = Aggregator::default();
        aggregator.set_rep_weights(rep_weights, Amount::nano(150_000));

        let preproposal1 =
            Preproposal::new(vec![(Account::from(1), BlockHash::from(10))], &rep_key);
        aggregator.add(preproposal1.clone());

        let preproposal2 =
            Preproposal::new(vec![(Account::from(2), BlockHash::from(20))], &rep_key);
        aggregator.add(preproposal2.clone());

        assert_eq!(aggregator.has_quorum(), false, "Should not reach quorum");
        assert_eq!(aggregator.len(), 1, "Should only contain one preproposal");
        assert!(
            aggregator.contains(&preproposal1.hash()),
            "Should contain preproposal1"
        );
    }

    fn test_frontiers() -> Vec<(Account, BlockHash)> {
        vec![(Account::from(1), BlockHash::from(10))]
    }
}
