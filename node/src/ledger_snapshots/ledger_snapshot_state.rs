use crate::ledger_snapshots::Aggregator;
use rsnano_messages::{Preproposal, Proposal, ProposalVote};

#[derive(Default)]
pub(crate) struct LedgerSnapshotState {
    pub(crate) preproposal_aggregator: Aggregator<Preproposal>,
    pub(crate) proposal_aggregator: Aggregator<Proposal>,
    pub(crate) vote_aggregator: Aggregator<ProposalVote>,
}
