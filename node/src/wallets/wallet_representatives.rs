use rsnano_core::{Account, Amount, PublicKey};
use rsnano_ledger::RepWeightCache;
use std::sync::Arc;

#[derive(Clone)]
pub struct WalletRepresentatives {
    voting_enabled: bool,
    /// has representatives with at least 50% of principal representative requirements
    half_principal: bool,
    /// Representatives with at least the configured minimum voting weight
    rep_keys: Vec<PublicKey>,
    vote_minimum: Amount,
    rep_weights: Arc<RepWeightCache>,
}

impl WalletRepresentatives {
    pub fn new(
        voting_enabled: bool,
        vote_minimum: Amount,
        rep_weights: Arc<RepWeightCache>,
    ) -> Self {
        Self {
            voting_enabled,
            half_principal: false,
            rep_keys: Vec::new(),
            vote_minimum,
            rep_weights,
        }
    }

    pub fn have_half_rep(&self) -> bool {
        self.half_principal
    }

    #[cfg(test)]
    pub fn set_have_half_rep(&mut self, value: bool) {
        self.half_principal = value;
    }

    pub fn voting_enabled(&self) -> bool {
        self.voting_enabled && self.voting_reps() > 0
    }

    pub fn voting_reps(&self) -> usize {
        self.rep_keys.len()
    }

    pub fn exists(&self, rep: &Account) -> bool {
        self.rep_keys.iter().any(|k| k.as_account() == *rep)
    }

    pub fn rep_keys(&self) -> impl Iterator<Item = PublicKey> + use<'_> {
        self.rep_keys.iter().cloned()
    }

    pub fn rep_accounts(&self) -> impl Iterator<Item = Account> + use<'_> {
        self.rep_keys.iter().map(|k| k.as_account())
    }

    pub fn clear(&mut self) {
        self.half_principal = false;
        self.rep_keys.clear();
    }

    pub fn check_rep(&mut self, pub_key: PublicKey, half_principal_weight: Amount) -> bool {
        let weight = self.rep_weights.weight(&pub_key);

        if weight < self.vote_minimum {
            return false; // account not a representative
        }

        if weight >= half_principal_weight {
            self.half_principal = true;
        }

        self.insert(pub_key)
    }

    fn insert(&mut self, pub_key: impl Into<PublicKey>) -> bool {
        let rep_key = pub_key.into();
        if self.rep_keys.contains(&rep_key) {
            return false;
        }

        self.rep_keys.push(rep_key);
        true
    }
}

impl Default for WalletRepresentatives {
    fn default() -> Self {
        Self::new(false, Amount::nano(1), Arc::new(RepWeightCache::new()))
    }
}
