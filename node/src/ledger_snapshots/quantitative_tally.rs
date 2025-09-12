use crate::ledger_snapshots::Aggregator;
use rsnano_ledger::RepWeights;
use rsnano_messages::Aggregatable;
use rsnano_types::Amount;

/// Quorum is reached if all received valid values have 67% vote weight in sum
pub(crate) struct QuantitativeTally {
    pub(crate) rep_weights: RepWeights,
    pub(crate) quorum_weight: Amount,
}

impl Default for QuantitativeTally {
    fn default() -> Self {
        Self {
            rep_weights: Default::default(),
            quorum_weight: Amount::MAX,
        }
    }
}

impl QuantitativeTally {
    pub(crate) fn set_rep_weights(&mut self, rep_weights: RepWeights, quorum_weight: Amount) {
        self.rep_weights = rep_weights;
        self.quorum_weight = quorum_weight;
    }

    pub(crate) fn has_quorum<T: Aggregatable>(&self, aggregator: &Aggregator<T>) -> bool {
        let mut weight = Amount::ZERO;
        for value in aggregator.values() {
            weight += self.rep_weights.weight(&value.signer());
        }
        weight >= self.quorum_weight
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rsnano_messages::Preproposal;
    use rsnano_types::{Account, BlockHash, PrivateKey};

    #[test]
    fn default_quorum_weight_is_max() {
        let tally = QuantitativeTally::default();
        assert_eq!(tally.quorum_weight, Amount::MAX);
    }

    #[test]
    fn no_quorum_if_value_doesnt_have_enough_vote_weight() {
        let mut tally = QuantitativeTally::default();

        let rep_key = PrivateKey::from(1);
        let weight = Amount::nano(10_000);
        let mut rep_weights = RepWeights::new();
        rep_weights.insert(rep_key.public_key(), weight);
        tally.set_rep_weights(rep_weights, Amount::MAX);

        let mut aggregator = Aggregator::default();
        aggregator.add(Preproposal::new(Vec::new(), &rep_key));

        assert_eq!(tally.has_quorum(&aggregator), false);
    }

    #[test]
    fn reach_quorum() {
        let rep_key1 = PrivateKey::from(1);
        let rep_key2 = PrivateKey::from(2);

        let mut rep_weights = RepWeights::new();
        rep_weights.insert(rep_key1.public_key(), Amount::nano(100_000));
        rep_weights.insert(rep_key2.public_key(), Amount::nano(200_000));

        let mut aggregator = Aggregator::default();
        let mut tally = QuantitativeTally::default();
        tally.set_rep_weights(rep_weights, Amount::nano(300_000));

        let preproposal1 = Preproposal::new(test_frontiers(), &rep_key1);
        aggregator.add(preproposal1.clone());
        let preproposal2 = Preproposal::new(test_frontiers(), &rep_key2);
        aggregator.add(preproposal2.clone());

        assert_eq!(tally.has_quorum(&aggregator), true);
    }

    fn test_frontiers() -> Vec<(Account, BlockHash)> {
        vec![(Account::from(1), BlockHash::from(10))]
    }
}
