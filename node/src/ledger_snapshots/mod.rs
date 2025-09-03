use rsnano_ledger::Ledger;
use rsnano_messages::Preproposal;
use rsnano_types::PrivateKey;
use rsnano_types::{Account, BlockHash};
use std::sync::Arc;

pub struct LedgerSnapshots {
    ledger: Arc<Ledger>,
    /// For simplicity we currently assume that there is at most
    /// one representative key!
    /// TODO: We have to extend this later to multiple representatives per node.
    get_private_key: Box<dyn Fn() -> Option<PrivateKey> + Send + Sync>,
}

impl LedgerSnapshots {
    pub fn new(
        ledger: Arc<Ledger>,
        get_private_key: impl Fn() -> Option<PrivateKey> + Send + Sync + 'static,
    ) -> Self {
        Self {
            ledger,
            get_private_key: Box::new(get_private_key),
        }
    }

    pub fn publish_preproposal(&self) {
        // TODO create
        // TODO publish
    }

    pub fn create_preproposal(&self, private_key: &PrivateKey) -> Preproposal {
        let frontiers = self.collect_frontiers();
        Preproposal::new(frontiers, private_key)
    }

    pub fn collect_frontiers(&self) -> Vec<(Account, BlockHash)> {
        self.ledger.confirmed().frontiers().collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::transport::{FloodEvent, MessageFlooder};
    use rsnano_ledger::Ledger;
    use rsnano_messages::Message;
    use rsnano_network::TrafficType;
    use rsnano_types::{AccountInfo, ConfirmationHeightInfo};

    #[test]
    fn ledger_with_one_account() {
        let account = Account::from(1);
        let frontier = BlockHash::from(2);
        let ledger = create_ledger_with_frontiers([(account, frontier)]);
        let snapshots = LedgerSnapshots::new(ledger.clone(), get_test_key);
        assert_eq!(snapshots.collect_frontiers(), [(account, frontier)]);
    }

    #[test]
    fn ledger_with_multiple_accounts() {
        let account1 = Account::from(1);
        let frontier1 = BlockHash::from(100);
        let account2 = Account::from(2);
        let frontier2 = BlockHash::from(200);

        let ledger = create_ledger_with_frontiers([(account1, frontier1), (account2, frontier2)]);
        let snapshots = LedgerSnapshots::new(ledger.clone(), get_test_key);
        assert_eq!(
            snapshots.collect_frontiers(),
            [(account1, frontier1), (account2, frontier2)]
        );
    }

    #[test]
    fn create_preproposal() {
        let account = Account::from(10);
        let frontier = BlockHash::from(2);
        let ledger = create_ledger_with_frontiers([(account, frontier)]);
        let snapshots = LedgerSnapshots::new(ledger.clone(), get_test_key);

        let preproposal = snapshots.create_preproposal(&PrivateKey::from(1));

        assert!(preproposal.frontiers.contains(&(account, frontier)));
    }

    #[test]
    #[ignore = "TODO"]
    fn publish_preproposal() {
        let account = Account::from(1);
        let frontier = BlockHash::from(100);
        let ledger = create_ledger_with_frontiers([(account, frontier)]);
        let flooder = MessageFlooder::new_null();
        let flood_tracker = flooder.track_floods();
        let snapshots = LedgerSnapshots::new(ledger.clone(), get_test_key);

        snapshots.publish_preproposal();

        let flood_events = flood_tracker.output();
        assert_eq!(flood_events.len(), 1);

        let expected_preproposal = snapshots.create_preproposal(&get_test_key().unwrap());
        assert_eq!(
            flood_events[0],
            FloodEvent {
                message: Message::SnapshotPreproposal(expected_preproposal),
                // TODO: add new traffic type for snapshots
                traffic_type: TrafficType::Generic,
                scale: 0.0,
                all_prs: true,
            }
        );
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
