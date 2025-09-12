use rsnano_node::config::NodeConfig;
use rsnano_types::{Amount, DEV_GENESIS_KEY};
use rsnano_utils::stats::{DetailType, Direction, StatType};
use test_helpers::{System, assert_timely_eq2, assert_timely2, setup_rep};

#[test]
fn publish_preproposal_integration_test() {
    let mut system = System::new();

    let node1 = system
        .build_node()
        .config(NodeConfig {
            enable_voting: true,
            ..System::default_config()
        })
        .finish();
    node1.insert_into_wallet(&DEV_GENESIS_KEY);

    let node2 = system
        .build_node()
        .config(NodeConfig {
            enable_voting: true,
            ..System::default_config()
        })
        .finish();
    let amount_pr = Amount::nano(2_000_000);
    let rep2_key = setup_rep(&node2, amount_pr, &DEV_GENESIS_KEY);
    node2.insert_into_wallet(&rep2_key);

    assert_timely2(|| {
        node1
            .online_reps
            .lock()
            .unwrap()
            .peered_principal_reps()
            .len()
            == 2
            && node2
                .online_reps
                .lock()
                .unwrap()
                .peered_principal_reps()
                .len()
                == 2
    });

    node1.ledger_snapshots.publish_preproposal();

    assert_timely_eq2(
        || {
            node2
                .stats
                .count(StatType::Message, DetailType::Preproposal, Direction::In)
        },
        1,
    );

    assert_timely_eq2(
        || {
            node1
                .stats
                .count(StatType::Message, DetailType::Preproposal, Direction::In)
        },
        1,
    );

    assert_timely_eq2(
        || {
            node2
                .stats
                .count(StatType::Message, DetailType::Proposal, Direction::In)
        },
        2,
    );

    assert_timely_eq2(
        || {
            node1
                .stats
                .count(StatType::Message, DetailType::Proposal, Direction::In)
        },
        2,
    );
}
