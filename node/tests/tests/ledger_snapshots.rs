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

    let node = system.make_node();
    let wallet_id = node.wallets.wallet_ids()[0];
    node
        .wallets
        .insert_adhoc2(&wallet_id, &DEV_GENESIS_KEY.raw_key(), true)
        .unwrap();

    let node1 = system.make_node();
    let node2 = system.make_node();
    let node3 = system.make_node();
    let key_non_pr = PrivateKey::new();
    let key_pr = PrivateKey::new();
    let amount_pr = Amount::nano(600_000);
    let amount_not_pr = Amount::nano(10_000);

    let mut lattice = UnsavedBlockLatticeBuilder::new();
    let send_non_pr = lattice.genesis().send(&key_non_pr, amount_not_pr);
    let open_no_pr = lattice.account(&key_non_pr).receive(&send_non_pr);
    let send_pr = lattice.genesis().send(&key_pr, amount_pr);
    let open_pr = lattice.account(&key_pr).receive(&send_pr);
    let blocks = [send_non_pr, open_no_pr, send_pr, open_pr];
    node.process_multi(&blocks);
    node1.process_multi(&blocks);
    node2.process_multi(&blocks);
    node3.process_multi(&blocks);
    //assert_eq!(node.online_reps.lock().unwrap().online_reps().count(), 0);

    assert_timely_eq2(
        || {
            node.network
                .read()
                .unwrap()
                .count_by_mode(ChannelMode::Realtime)
        },
        3,
    );

    let (channel1, channel2, channel3) = {
        let network = node.network.read().unwrap();
        (
            network.find_node_id(&node1.get_node_id()).unwrap().clone(),
            network.find_node_id(&node2.get_node_id()).unwrap().clone(),
            network.find_node_id(&node3.get_node_id()).unwrap().clone(),
        )
    };

    let vote0 = ReceivedVote::new(
        Arc::new(Vote::new(
            &DEV_GENESIS_KEY,
            UnixMillisTimestamp::ZERO,
            0,
            vec![*DEV_GENESIS_HASH],
        )),
        VoteSource::Live,
        Some(channel1.clone()),
    );

    let vote1 = ReceivedVote::new(
        Arc::new(Vote::new(
            &key_non_pr,
            UnixMillisTimestamp::ZERO,
            0,
            vec![*DEV_GENESIS_HASH],
        )),
        VoteSource::Live,
        Some(channel2.clone()),
    );

    let vote2 = ReceivedVote::new(
        Arc::new(Vote::new(
            &key_pr,
            UnixMillisTimestamp::ZERO,
            0,
            vec![*DEV_GENESIS_HASH],
        )),
        VoteSource::Live,
        Some(channel3.clone()),
    );

    node.rep_crawler.force_process2(vote0);
    node.rep_crawler.force_process2(vote1);
    node.rep_crawler.force_process2(vote2);

    assert_timely_eq2(|| node.online_reps.lock().unwrap().peered_reps_count(), 2);
    // Make sure we get the rep with the most weight first
    let rep = node.online_reps.lock().unwrap().peered_reps()[0].clone();
    assert_eq!(
        node.balance(&DEV_GENESIS_ACCOUNT),
        node.ledger.weight(&rep.rep_key)
    );
    assert_eq!(channel1, rep.channel);
    assert_eq!(
        node.online_reps
            .lock()
            .unwrap()
            .is_principal_rep(channel1.channel_id()),
        true
    );
    assert_eq!(
        node.online_reps
            .lock()
            .unwrap()
            .is_principal_rep(channel2.channel_id()),
        false
    );
    assert_eq!(
        node.online_reps
            .lock()
            .unwrap()
            .is_principal_rep(channel3.channel_id()),
        true
    );

    node.ledger_snapshots.publish_preproposal();

    assert_timely2(|| {
        node1.stats.count(StatType::TcpServer, DetailType::Preproposal, Direction::In) > 0
    });
}