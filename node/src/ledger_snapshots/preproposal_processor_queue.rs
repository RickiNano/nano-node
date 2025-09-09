use std::sync::{Arc, Mutex};

use rsnano_ledger::RepWeightCache;
use rsnano_messages::Preproposal;
use rsnano_types::Amount;

/// Minimal preproposal processor queue which only accumulates
/// voting weight of received preproposals' signers.
pub struct PreproposalProcessorQueue {
    rep_weights: Arc<RepWeightCache>,
    accumulated_weight: Mutex<Amount>,
}

impl PreproposalProcessorQueue {
    pub fn new(rep_weights: Arc<RepWeightCache>) -> Self {
        Self {
            rep_weights,
            accumulated_weight: Mutex::new(Amount::ZERO),
        }
    }

    /// Enqueue a preproposal and add the signer's weight
    /// to the accumulated total.
    pub fn enqueue(&self, preproposal: &Preproposal) {
        let weight = self.rep_weights.weight(&preproposal.signer);
        let mut guard = self.accumulated_weight.lock().unwrap();
        *guard += weight;
    }

    /// Returns the accumulated voting weight so far.
    pub fn accumulated_weight(&self) -> Amount {
        *self.accumulated_weight.lock().unwrap()
    }

    /// Resets the accumulated voting weight to zero.
    pub fn clear(&self) {
        *self.accumulated_weight.lock().unwrap() = Amount::ZERO;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rsnano_ledger::{BootstrapWeights, RepWeights};
    use rsnano_store_lmdb::LedgerCache;
    use rsnano_types::{PrivateKey, PublicKey};

    fn rep_weights_with(map: &[(PublicKey, Amount)]) -> Arc<RepWeightCache> {
        let mut weights = RepWeights::new();
        for (k, v) in map.iter() {
            weights.insert(*k, *v);
        }
        let bootstrap = BootstrapWeights { weights, max_blocks: u64::MAX };
        Arc::new(RepWeightCache::with_bootstrap_weights(
            bootstrap,
            Arc::new(LedgerCache::new()),
        ))
    }

    #[test]
    fn accumulates_signer_weights() {
        let key1 = PrivateKey::from(1).public_key();
        let key2 = PrivateKey::from(2).public_key();
        let w1 = Amount::from(5);
        let w2 = Amount::from(7);
        let cache = rep_weights_with(&[(key1, w1), (key2, w2)]);
        let queue = PreproposalProcessorQueue::new(cache);

        let mut p1 = Preproposal::new_test_instance();
        p1.signer = key1;
        let mut p2 = Preproposal::new_test_instance();
        p2.signer = key2;

        queue.enqueue(&p1);
        queue.enqueue(&p2);

        assert_eq!(queue.accumulated_weight(), w1 + w2);
    }

    #[test]
    fn clear_resets_accumulated_weight() {
        let key = PrivateKey::from(3).public_key();
        let w = Amount::from(9);
        let cache = rep_weights_with(&[(key, w)]);
        let queue = PreproposalProcessorQueue::new(cache);

        let mut p = Preproposal::new_test_instance();
        p.signer = key;
        queue.enqueue(&p);
        assert_eq!(queue.accumulated_weight(), w);

        queue.clear();
        assert_eq!(queue.accumulated_weight(), Amount::ZERO);
    }
}


