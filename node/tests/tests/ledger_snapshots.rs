use std::sync::Arc;
use rsnano_ledger::{test_helpers::UnsavedBlockLatticeBuilder, DEV_GENESIS_ACCOUNT, DEV_GENESIS_HASH};
use rsnano_network::ChannelMode;
use rsnano_node::{consensus::ReceivedVote};
use rsnano_types::{Amount, PrivateKey, UnixMillisTimestamp, Vote, VoteSource, DEV_GENESIS_KEY};
use rsnano_utils::stats::{DetailType, Direction, StatType};
use test_helpers::{assert_timely2, assert_timely_eq2, System};

#[test]
fn publish_preproposal() {
    let mut system = System::new();

    let node1 = system.make_node();
    let wallet_id = node1.wallets.wallet_ids()[0];
    node1
        .wallets
        .insert_adhoc2(&wallet_id, &DEV_GENESIS_KEY.raw_key(), true)
        .unwrap();

    let key_pr = PrivateKey::new();
    let amount_pr = Amount::nano(600_000);

    let node2 = system.make_node();
    let wallet_id = node2.wallets.wallet_ids()[0];
    node2
        .wallets
        .insert_adhoc2(&wallet_id, &key_pr.raw_key(), true)
        .unwrap();

    let mut lattice = UnsavedBlockLatticeBuilder::new();
    let send_pr = lattice.genesis().send(&key_pr, amount_pr);
    let open_pr = lattice.account(&key_pr).receive(&send_pr);
    let blocks = [send_pr, open_pr];
    node1.process_multi(&blocks);
    node2.process_multi(&blocks);
    assert_eq!(node1.online_reps.lock().unwrap().online_reps().count(), 0);

    assert_timely_eq2(
        || {
            node1.network
                .read()
                .unwrap()
                .count_by_mode(ChannelMode::Realtime)
        },
        1,
    );

    let channel1 = {
        let network = node1.network.read().unwrap();
            network.find_node_id(&node2.get_node_id()).unwrap().clone()
    };

    let vote0 = ReceivedVote::new(
        Arc::new(Vote::new(
            &key_pr,
            UnixMillisTimestamp::ZERO,
            0,
            vec![*DEV_GENESIS_HASH],
        )),
        VoteSource::Live,
        Some(channel1.clone()),
    );

    node1.rep_crawler.force_process2(vote0);

    assert_timely_eq2(|| node1.online_reps.lock().unwrap().peered_reps_count(), 2);
    // Make sure we get the rep with the most weight first
    let rep = node1.online_reps.lock().unwrap().peered_reps()[0].clone();
    assert_eq!(
        node1.balance(&DEV_GENESIS_ACCOUNT),
        node1.ledger.weight(&rep.rep_key)
    );
    let rep = node1.online_reps.lock().unwrap().peered_reps()[1].clone();
    assert_eq!(channel1, rep.channel);
    assert_eq!(
        node1.online_reps
            .lock()
            .unwrap()
            .is_principal_rep(channel1.channel_id()),
        true
    );

    node1.ledger_snapshots.publish_preproposal();

    assert_timely2(|| {
        node2.stats.count(StatType::TcpServer, DetailType::Preproposal, Direction::In) > 0
    });
}