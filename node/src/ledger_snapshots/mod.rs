mod aggregator;

use std::sync::{Arc, Mutex};

use rsnano_ledger::Ledger;
use rsnano_messages::{Aggregatable, Message, Preproposal, Proposal, ProposalVote};
use rsnano_network::TrafficType;
use rsnano_output_tracker::{OutputListenerMt, OutputTrackerMt};
use rsnano_types::PrivateKey;
use rsnano_types::{Account, BlockHash};

use crate::ledger_snapshots::aggregator::Aggregator;
use crate::representatives::OnlineReps;
use crate::transport::MessageFlooder;

pub struct LedgerSnapshots {
    ledger: Arc<Ledger>,
    /// For simplicity we currently assume that there is at most
    /// one representative key!
    /// TODO: We have to extend this later to multiple representatives per node.
    get_private_key: Box<dyn Fn() -> Option<PrivateKey> + Send + Sync>,
    flooder: Mutex<MessageFlooder>,
    receive_preproposal_listener: OutputListenerMt<Preproposal>,
    receive_proposal_listener: OutputListenerMt<Proposal>,
    preproposal_aggregator: Mutex<Aggregator<Preproposal>>,
    proposal_aggregator: Mutex<Aggregator<Proposal>>,
    online_reps: Arc<Mutex<OnlineReps>>,
}

impl LedgerSnapshots {
    pub fn new(
        ledger: Arc<Ledger>,
        get_private_key: impl Fn() -> Option<PrivateKey> + Send + Sync + 'static,
        flooder: MessageFlooder,
        online_reps: Arc<Mutex<OnlineReps>>,
    ) -> Self {
        Self {
            ledger,
            get_private_key: Box::new(get_private_key),
            flooder: flooder.into(),
            receive_preproposal_listener: OutputListenerMt::new(),
            receive_proposal_listener: OutputListenerMt::new(),
            preproposal_aggregator: Default::default(),
            proposal_aggregator: Default::default(),
            online_reps,
        }
    }

    pub fn new_null() -> Self {
        Self::new(
            Ledger::new_null().into(),
            || None,
            MessageFlooder::new_null(),
            Mutex::new(OnlineReps::default()).into(),
        )
    }

    pub fn publish_preproposal(&self) {
        // TODO add test for no private key
        let private_key = (self.get_private_key)().unwrap();
        let preproposal = self.create_preproposal(&private_key);
        let message = Message::SnapshotPreproposal(preproposal);
        self.flooder.lock().unwrap().flood_prs_and_some_non_prs(
            &message,
            TrafficType::LedgerSnapshots,
            0.0,
        );
    }

    fn create_preproposal(&self, private_key: &PrivateKey) -> Preproposal {
        let frontiers = self.collect_frontiers();
        Preproposal::new(frontiers, private_key)
    }

    fn collect_frontiers(&self) -> Vec<(Account, BlockHash)> {
        self.ledger.confirmed().frontiers().collect()
    }

    pub fn receive_preproposal(&self, preproposal: Preproposal) {
        self.receive_preproposal_listener.emit(preproposal.clone());

        let (rep_weights, quorum_weight) = self.get_consensus_info();

        let proposal = {
            let mut preproposal_aggregator = self.preproposal_aggregator.lock().unwrap();
            preproposal_aggregator.add(preproposal);
            preproposal_aggregator.set_rep_weights(rep_weights, quorum_weight);

            if preproposal_aggregator.has_quorum() {
                let proposal = Proposal::new(
                    preproposal_aggregator.values(),
                    &(self.get_private_key)().unwrap(),
                );
                Some(proposal)
            } else {
                None
            }
        };

        if let Some(proposal) = proposal {
            self.flooder.lock().unwrap().flood_prs_and_some_non_prs(
                &Message::SnapshotProposal(proposal),
                TrafficType::LedgerSnapshots,
                0.0,
            );
        };
    }

    fn get_consensus_info(&self) -> (rsnano_ledger::RepWeights, rsnano_types::Amount) {
        let online_reps = self.online_reps.lock().unwrap();
        let rep_weights = online_reps.get_rep_weights();
        let quorum_weight = online_reps.quorum_delta();
        (rep_weights, quorum_weight)
    }

    pub fn track_received_preproposals(&self) -> Arc<OutputTrackerMt<Preproposal>> {
        self.receive_preproposal_listener.track()
    }

    pub fn receive_proposal(&self, proposal: Proposal) {
        self.receive_proposal_listener.emit(proposal.clone());

        let (rep_weights, quorum_weight) = self.get_consensus_info();

        let mut proposal_aggregator = self.proposal_aggregator.lock().unwrap();
        proposal_aggregator.add(proposal);

        proposal_aggregator.set_rep_weights(rep_weights, quorum_weight);

        if proposal_aggregator.has_quorum() {
            self.flooder.lock().unwrap().flood_prs_and_some_non_prs(
                &Message::SnapshotProposalVote(ProposalVote::new(
                    proposal_aggregator.values().next().unwrap().hash(),
                    &(self.get_private_key)().unwrap(),
                )),
                TrafficType::LedgerSnapshots,
                0.0,
            );
        }
    }

    pub fn track_received_proposals(&self) -> Arc<OutputTrackerMt<Proposal>> {
        self.receive_proposal_listener.track()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{representatives::ONLINE_WEIGHT_QUORUM, transport::FloodEvent};
    use rsnano_ledger::RepWeights;
    use rsnano_messages::{Aggregatable, Message, ProposalVote};
    use rsnano_network::TrafficType;
    use rsnano_output_tracker::OutputTrackerMt;
    use rsnano_types::{AccountInfo, Amount, ConfirmationHeightInfo};
    use std::time::Duration;

    #[test]
    fn ledger_with_one_account() {
        let account = Account::from(1);
        let frontier = BlockHash::from(2);
        let fixture = Fixture::with_frontiers([(account, frontier)]);
        assert_eq!(fixture.snapshots.collect_frontiers(), [(account, frontier)]);
    }

    #[test]
    fn ledger_with_multiple_accounts() {
        let account1 = Account::from(1);
        let frontier1 = BlockHash::from(100);
        let account2 = Account::from(2);
        let frontier2 = BlockHash::from(200);

        let fixture = Fixture::with_frontiers([(account1, frontier1), (account2, frontier2)]);
        assert_eq!(
            fixture.snapshots.collect_frontiers(),
            [(account1, frontier1), (account2, frontier2)]
        );
    }

    #[test]
    fn create_preproposal() {
        let account = Account::from(10);
        let frontier = BlockHash::from(2);
        let fixture = Fixture::with_frontiers([(account, frontier)]);

        let preproposal = fixture.snapshots.create_preproposal(&PrivateKey::from(1));

        assert!(preproposal.frontiers.contains(&(account, frontier)));
    }

    #[test]
    fn publish_preproposal() {
        let account = Account::from(1);
        let frontier = BlockHash::from(100);
        let fixture = Fixture::with_frontiers([(account, frontier)]);

        fixture.snapshots.publish_preproposal();

        let flood_events = fixture.flood_tracker.output();
        assert_eq!(flood_events.len(), 1, "Should flood the message");

        let expected_preproposal = fixture
            .snapshots
            .create_preproposal(&get_test_key().unwrap());

        assert_eq!(
            flood_events[0],
            FloodEvent {
                message: Message::SnapshotPreproposal(expected_preproposal),
                // TODO: add new traffic type for snapshots
                traffic_type: TrafficType::LedgerSnapshots,
                scale: 0.0,
                all_prs: true,
            }
        );
    }

    #[test]
    fn receive_preproposal_listener() {
        let fixture = Fixture::new();
        let preproposal = Preproposal::new_test_instance();
        fixture.snapshots.receive_preproposal(preproposal.clone());

        let receive_events = fixture.receive_preproposal_tracker.output();
        assert_eq!(receive_events.len(), 1, "Should receive preproposal");
        assert_eq!(receive_events[0], preproposal);
    }

    #[test]
    fn a_received_preproposal_is_added_to_the_preproposal_aggregator() {
        let fixture = Fixture::new();
        let snapshots = &fixture.snapshots;
        let preproposal = Preproposal::new_test_instance();

        snapshots.receive_preproposal(preproposal.clone());

        assert!(
            snapshots
                .preproposal_aggregator
                .lock()
                .unwrap()
                .contains(&preproposal.hash())
        );
    }

    #[test]
    fn a_received_preproposal_sets_the_rep_weights() {
        let fixture = Fixture::new();
        let snapshots = &fixture.snapshots;
        let preproposal = Preproposal::new_test_instance();

        snapshots.receive_preproposal(preproposal.clone());
        let online_reps = snapshots.online_reps.lock().unwrap();
        let preproposal_aggregator = snapshots.preproposal_aggregator.lock().unwrap();

        assert_eq!(
            preproposal_aggregator.quorum_weight,
            online_reps.quorum_delta()
        );
        assert_eq!(
            preproposal_aggregator.rep_weights,
            online_reps.get_rep_weights()
        );
    }

    #[test]
    fn publish_proposal_vote_when_quorum_of_preproposals_is_reached() {
        let mut rep_weights = RepWeights::new();
        let private_key = get_test_key().unwrap();
        let quorum_weight = Amount::nano(100_000);

        rep_weights.insert(private_key.public_key(), quorum_weight);
        let fixture = Fixture::with_rep_weights(rep_weights, quorum_weight);

        let preproposal = Preproposal::new(vec![], &private_key);
        fixture.snapshots.receive_preproposal(preproposal.clone());

        let flood_events = fixture.flood_tracker.output();
        assert_eq!(flood_events.len(), 1, "Should flood the message");

        let expected_proposal = Proposal::new(&[preproposal], &private_key);

        assert_eq!(
            flood_events[0],
            FloodEvent {
                message: Message::SnapshotProposal(expected_proposal),
                traffic_type: TrafficType::LedgerSnapshots,
                scale: 0.0,
                all_prs: true,
            }
        );
    }

    #[test]
    fn receive_proposal_listener() {
        let fixture = Fixture::new();
        let proposal = Proposal::new_test_instance();
        fixture.snapshots.receive_proposal(proposal.clone());

        let receive_events = fixture.receive_proposal_tracker.output();
        assert_eq!(receive_events.len(), 1, "Should receive proposal");
        assert_eq!(receive_events[0], proposal);
    }

    #[test]
    fn a_received_proposal_is_added_to_the_proposal_aggregator() {
        let fixture = Fixture::new();
        let snapshots = &fixture.snapshots;
        let proposal = Proposal::new_test_instance();

        snapshots.receive_proposal(proposal.clone());

        assert!(
            snapshots
                .proposal_aggregator
                .lock()
                .unwrap()
                .contains(&proposal.hash())
        );
    }

    #[test]
    fn a_received_proposal_sets_the_rep_weights() {
        let fixture = Fixture::new();
        let snapshots = &fixture.snapshots;
        let proposal = Proposal::new_test_instance();

        snapshots.receive_proposal(proposal.clone());
        let online_reps = snapshots.online_reps.lock().unwrap();
        let proposal_aggregator = snapshots.proposal_aggregator.lock().unwrap();

        assert_eq!(
            proposal_aggregator.quorum_weight,
            online_reps.quorum_delta()
        );
        assert_eq!(
            proposal_aggregator.rep_weights,
            online_reps.get_rep_weights()
        );
    }

    #[test]
    fn publish_proposal_vote_when_quorum_of_proposals_is_reached() {
        let mut rep_weights = RepWeights::new();
        let private_key = get_test_key().unwrap();
        let quorum_weight = Amount::nano(100_000);

        rep_weights.insert(private_key.public_key(), quorum_weight);
        let fixture = Fixture::with_rep_weights(rep_weights, quorum_weight);

        let proposal = Proposal::new(vec![], &private_key);
        fixture.snapshots.receive_proposal(proposal.clone());

        let flood_events = fixture.flood_tracker.output();
        assert_eq!(flood_events.len(), 1, "Should flood the message");

        let expected_proposal_vote = ProposalVote::new(proposal.hash(), &private_key);

        assert_eq!(
            flood_events[0],
            FloodEvent {
                message: Message::SnapshotProposalVote(expected_proposal_vote),
                traffic_type: TrafficType::LedgerSnapshots,
                scale: 0.0,
                all_prs: true,
            }
        );
    }

    struct Fixture {
        snapshots: LedgerSnapshots,
        flood_tracker: Arc<OutputTrackerMt<FloodEvent>>,
        receive_preproposal_tracker: Arc<OutputTrackerMt<Preproposal>>,
        receive_proposal_tracker: Arc<OutputTrackerMt<Proposal>>,
    }

    impl Fixture {
        fn new() -> Self {
            Self::with_frontiers([])
        }

        fn with_frontiers(frontiers: impl IntoIterator<Item = (Account, BlockHash)>) -> Self {
            let ledger = create_ledger_with_frontiers(frontiers);
            Self::with_ledger(ledger)
        }

        fn with_ledger(ledger: Arc<Ledger>) -> Self {
            Self::with_ledger_and_weights(ledger, RepWeights::new(), Amount::nano(60_000_000))
        }

        fn with_rep_weights(rep_weights: RepWeights, quorum_weight: Amount) -> Self {
            let ledger = create_ledger_with_frontiers([]);
            Self::with_ledger_and_weights(ledger, rep_weights, quorum_weight)
        }

        fn with_ledger_and_weights(
            ledger: Arc<Ledger>,
            rep_weights: RepWeights,
            quorum_weight: Amount,
        ) -> Self {
            let flooder = MessageFlooder::new_null();
            let flood_tracker = flooder.track_floods();

            let mut online_reps = OnlineReps::new(
                Arc::new(rep_weights.into()),
                Duration::ZERO,
                Amount::ZERO,
                Amount::ZERO,
            );
            online_reps.set_trended(quorum_weight / ONLINE_WEIGHT_QUORUM as u128 * 100);
            let online_reps = Arc::new(Mutex::new(online_reps));

            let snapshots =
                LedgerSnapshots::new(ledger.clone(), get_test_key, flooder, online_reps);

            let receive_preproposal_tracker = snapshots.track_received_preproposals();
            let receive_proposal_tracker = snapshots.track_received_proposals();

            Self {
                snapshots,
                flood_tracker,
                receive_preproposal_tracker,
                receive_proposal_tracker,
            }
        }
    }

    fn get_test_key() -> Option<PrivateKey> {
        Some(PrivateKey::from(123))
    }

    fn create_ledger_with_frontiers(
        frontiers: impl IntoIterator<Item = (Account, BlockHash)>,
    ) -> Arc<Ledger> {
        let mut builder = Ledger::new_null_builder();

        for (account, frontier) in frontiers {
            builder = builder
                .account_info(&account, &AccountInfo::new_test_instance())
                .confirmation_height(
                    &account,
                    &ConfirmationHeightInfo {
                        height: 0,
                        frontier,
                    },
                );
        }

        builder.finish().into()
    }
}
