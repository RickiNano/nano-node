use rsnano_messages::PreproposalHash;

#[derive(Default)]
pub(super) struct PreproposalAggregator {}

impl PreproposalAggregator {
    pub fn contains(&self, hash: &PreproposalHash) -> bool {
        false
    }
}
