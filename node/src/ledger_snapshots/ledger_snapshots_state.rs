use crate::{
    ledger_snapshots::{Aggregator, tally::find_winner_proposal},
    representatives::ConsensusParams,
};
use rsnano_messages::{Aggregatable, Preproposal, Proposal, ProposalVote};
use rsnano_types::{PrivateKey, SnapshotNumber};

#[derive(Default)]
pub(crate) struct LedgerSnapshotsState {
    pub(crate) preproposal_aggregator: Aggregator<Preproposal>,
    pub(crate) proposal_aggregator: Aggregator<Proposal>,
    pub(crate) vote_aggregator: Aggregator<ProposalVote>,
    pub(crate) proposal_voted: bool,
    pub(crate) current_snapshot_number: u32,
}

impl LedgerSnapshotsState {
    pub(crate) fn receive_preproposal(&mut self, preproposal: Preproposal) -> bool {
        if preproposal.snapshot_number != self.current_snapshot_number {
            return false;
        }
        self.preproposal_aggregator.add(preproposal);
        true
    }

    pub(crate) fn try_create_proposal(
        &self,
        consensus_params: &ConsensusParams,
        rep_key: &PrivateKey,
    ) -> Option<Proposal> {
        if self.preproposal_aggregator.has_quorum(consensus_params) {
            let proposal = Proposal::new(
                self.preproposal_aggregator.values(),
                rep_key,
                self.current_snapshot_number,
            );
            Some(proposal)
        } else {
            None
        }
    }

    pub(crate) fn receive_proposal(&mut self, proposal: Proposal) -> bool {
        if proposal.snapshot_number != self.current_snapshot_number {
            return false;
        }

        self.proposal_aggregator.add(proposal);
        true
    }

    pub(crate) fn try_create_vote(
        &mut self,
        consensus_params: &ConsensusParams,
        rep_key: &PrivateKey,
    ) -> Option<ProposalVote> {
        let has_quorum = self.proposal_aggregator.has_quorum(&consensus_params);

        if has_quorum && !self.proposal_voted {
            let proposal_vote = create_proposal_vote(
                &self.proposal_aggregator,
                rep_key,
                self.current_snapshot_number,
            )
            .expect("Should always be able to create a vote when quorum reached");
            self.proposal_voted = true;
            Some(proposal_vote)
        } else {
            None
        }
    }

    pub(crate) fn receive_vote(
        &mut self,
        vote: ProposalVote,
        consensus_params: &ConsensusParams,
    ) -> bool {
        if vote.snapshot_number != self.current_snapshot_number {
            return false;
        }

        self.vote_aggregator.add(vote);

        if let Some(winner) = find_winner_proposal(&consensus_params, self.vote_aggregator.values())
        {
            tracing::warn!(proposal_hash=?winner, "Found a winner!");
            self.current_snapshot_number += 1;
            self.preproposal_aggregator.clear();
            self.proposal_aggregator.clear();
            self.vote_aggregator.clear();
        }

        true
    }
}

pub(crate) fn create_proposal_vote(
    aggregator: &Aggregator<Proposal>,
    private_key: &PrivateKey,
    snapshot_number: SnapshotNumber,
) -> Option<ProposalVote> {
    Some(ProposalVote::new(
        aggregator.values().map(|p| p.hash()).max()?,
        private_key,
        snapshot_number,
    ))
}
