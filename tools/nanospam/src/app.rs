use std::{
    ffi::OsString,
    net::{Ipv6Addr, SocketAddrV6},
    sync::Mutex,
    thread::yield_now,
    time::{Duration, Instant},
};

use num_format::{Locale, ToFormattedString};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt, ReadHalf, WriteHalf},
    select,
    sync::mpsc,
    task::JoinSet,
    time::timeout,
};
use tokio_util::sync::CancellationToken;
use tracing::info;

use rsnano_messages::{Message, MessageSerializer, Publish};
use rsnano_nullable_clock::{SteadyClock, Timestamp};
use rsnano_nullable_tcp::{TcpStream, TcpStreamFactory};
use rsnano_nullable_tracing_subscriber::TracingInitializer;
use rsnano_rpc_client::NanoRpcClient;
use rsnano_types::{Networks, PrivateKey, ProtocolInfo, RawKey, WalletId};
use rsnano_websocket_messages::MessageEnvelope;

use crate::{
    cli_args::Args,
    confirmation_receiver::ConfirmationReceiver,
    confirmation_tracker::track_confirmations,
    domain::{BlockResult, Forks, spam_logic::SpamLogic},
    frontiers_sync::sync_frontiers,
    handshake::perform_handshake,
    high_prio_check::HighPrioCheck,
    setup::{
        configure_nodes, create_account_map, get_genesis_hash, peering_port, rpc_port, start_nodes,
    },
    wallets_factory::create_wallets,
};
use clap::Parser;
use rand::{Rng, rng};

const MAX_BUFFERED_BLOCKS: usize = 1024;
const CONNECTIONS_PER_NODE: usize = 4;

#[derive(Default)]
pub(crate) struct NanoSpamApp {
    pub tracing_init: TracingInitializer,
    pub tcp_stream_factory: TcpStreamFactory,
}

impl NanoSpamApp {
    pub async fn run<I, T>(&self, args: I) -> anyhow::Result<()>
    where
        I: IntoIterator<Item = T>,
        T: Into<OsString> + Clone,
    {
        self.tracing_init.init();
        let args = Args::try_parse_from(args)?;
        let set_up_new_nodes = !args.attach && !args.sync;
        let clock = SteadyClock::default();

        let protocol = ProtocolInfo::default_for(Networks::NanoTestNetwork);
        let genesis_hash = get_genesis_hash();

        let mut data_dir = dirs::home_dir().unwrap();
        data_dir.push("NanoSpam");

        let mut account_map = create_account_map(&data_dir, args.accounts);

        if !args.attach && !args.sync {
            configure_nodes(&args, &data_dir);
        }

        let mut rpc_clients = Vec::new();
        for i in 0..args.prs {
            let rpc_client =
                NanoRpcClient::new(format!("http://[::1]:{}", rpc_port(i)).parse().unwrap());
            rpc_clients.push(rpc_client);
        }
        let genesis_rpc = &rpc_clients[0];

        if args.sync {
            sync_frontiers(&rpc_clients, &mut account_map).await;
        }

        let mut node_handles = Vec::new();

        if !args.attach {
            node_handles = start_nodes(&args, data_dir, &rpc_clients).await
        }

        let genesis_wallet_id = if set_up_new_nodes {
            create_wallets(&rpc_clients, genesis_rpc, &mut account_map).await
        } else {
            WalletId::ZERO
        };

        let logic = Mutex::new(SpamLogic::new(account_map, args.spam_spec()?));

        let (tx_blocks, rx_blocks) = mpsc::channel::<Forks>(MAX_BUFFERED_BLOCKS);
        let mut high_prio_check = HighPrioCheck::new(genesis_rpc, &logic);

        if set_up_new_nodes {
            high_prio_check
                .create_prio_accounts(genesis_wallet_id)
                .await?;
        }

        if args.setup_only {
            return Ok(());
        }

        if args.sync {
            high_prio_check.sync_accounts().await?;
        }

        let mut tcp_writers = Vec::new();
        let mut tcp_readers = Vec::new();

        for node_index in 0..args.prs {
            let peer_addr = SocketAddrV6::new(Ipv6Addr::LOCALHOST, peering_port(node_index), 0, 0);
            info!(?peer_addr, "Connecting to node PR{node_index}...");
            let mut node_writers = Vec::with_capacity(CONNECTIONS_PER_NODE);
            let mut node_readers = Vec::with_capacity(CONNECTIONS_PER_NODE);
            for i in 0..CONNECTIONS_PER_NODE {
                let mut tcp_stream = self.tcp_stream_factory.connect(peer_addr).await?;
                info!("Performing handshake...");
                let node_id_key: PrivateKey = RawKey::from(42 + i as u64).into();
                perform_handshake(protocol, genesis_hash, node_id_key, &mut tcp_stream).await?;
                let (tcp_read, tcp_write) = tokio::io::split(tcp_stream);
                node_writers.push(tcp_write);
                node_readers.push(tcp_read);
            }
            tcp_writers.push(node_writers);
            tcp_readers.push(node_readers);
        }

        let tx_forks_clone = tx_blocks.clone();
        let cancel_block_creation = CancellationToken::new();
        let cancel_blk = cancel_block_creation.clone();
        let cancel_tcp_recv = CancellationToken::new();
        let cancel_nanospam = CancellationToken::new();

        let (tx_ws_msg, rx_ws_msg) = std::sync::mpsc::channel::<(MessageEnvelope, Timestamp)>();

        info!("Connecting to websocket...");
        let mut conf_receiver = ConfirmationReceiver::connect().await?;

        info!("Starting with {} BPS", logic.lock().unwrap().current_bps);

        let started = Instant::now();
        std::thread::scope(|s| {
            s.spawn(|| {
                create_blocks(&logic, tx_blocks, &clock);
                cancel_blk.cancel();
            });

            s.spawn(|| track_confirmations(rx_ws_msg, &logic));

            tokio_scoped::scope(|scope| {
                scope.spawn(log_status(&logic, &clock, cancel_nanospam.clone()));

                if !args.no_prio {
                    scope.spawn(high_prio_check.run(cancel_block_creation, tx_forks_clone.clone()));
                }
                scope.spawn(conf_receiver.run(cancel_nanospam.clone(), tx_ws_msg, &clock));
                scope.spawn(receive_messages(
                    tcp_readers,
                    protocol,
                    cancel_tcp_recv.clone(),
                ));
                scope.spawn(publish_blocks(
                    rx_blocks,
                    tcp_writers,
                    protocol,
                    &logic,
                    cancel_tcp_recv,
                    args.unconfirmed,
                    args.drop_probability(),
                    &clock,
                ));
                if !args.no_republish {
                    scope.spawn(republish_delayed_blocks(
                        tx_forks_clone,
                        &logic,
                        cancel_nanospam,
                        &clock,
                    ));
                }
            });
        });
        let duration_secs = started.elapsed().as_secs_f64();
        let logic = logic.lock().unwrap();
        let created_blocks = logic.block_factory.created();
        let cps = (created_blocks as f64 / duration_secs) as i32;
        info!("Confirming {created_blocks} blocks took {duration_secs:.2}s");
        info!("Confirmation rate: {cps} cps");
        let conf_time = logic.sum_conf_time_total.as_millis() / created_blocks as u128;
        info!("Average conf time: {conf_time} ms");

        if !args.no_kill {
            for mut child in node_handles {
                child.kill().unwrap();
            }
        }
        Ok(())
    }
}

fn create_blocks(logic: &Mutex<SpamLogic>, tx_blocks: mpsc::Sender<Forks>, clock: &SteadyClock) {
    loop {
        let now = clock.now();

        let result = {
            let mut l = logic.lock().unwrap();
            let is_fork = rng().random_bool(l.fork_propability());
            l.next_block(is_fork, now)
        };

        match result {
            Some(BlockResult::Block(forks)) => {
                tx_blocks.blocking_send(forks).unwrap();
            }
            Some(BlockResult::Waiting) => {
                yield_now();
                continue;
            }
            None => {
                break;
            }
        };
    }
}

async fn publish_blocks(
    mut rx_forks: mpsc::Receiver<Forks>,
    mut tcp_streams: Vec<Vec<WriteHalf<TcpStream>>>,
    protocol: ProtocolInfo,
    logic: &Mutex<SpamLogic>,
    cancel_token: CancellationToken,
    unconfirmed: bool,
    drop_probability: f64,
    clock: &SteadyClock,
) {
    let mut serializer = MessageSerializer::new(protocol);
    let mut fork_serializer = MessageSerializer::new(protocol);
    let mut writer_index = 0;
    while let Some(forks) = rx_forks.recv().await {
        let block = forks.block.clone();
        let hash = block.hash();
        let publish = Message::Publish(Publish::new_from_originator(block));
        let buffer = serializer.serialize(&publish);
        let mut fork_buffer = None;

        if let Some(fork) = forks.fork {
            let publish_fork = Message::Publish(Publish::new_from_originator(fork));
            fork_buffer = Some(fork_serializer.serialize(&publish_fork));
        }

        let now = clock.now();
        // TODO support delayed forks
        logic.lock().unwrap().delayed.published(&hash, now);

        let mut counter = 0;
        tokio_scoped::scope(|s| {
            for stream in &mut tcp_streams {
                if rng().random_bool(drop_probability) {
                    // drop this transmission
                    continue;
                }

                let buf = if let Some(fbuf) = fork_buffer
                    && counter % 2 == 0
                {
                    fbuf
                } else {
                    buffer
                };

                s.spawn(async {
                    stream[writer_index].write_all(buf).await.unwrap();
                });

                counter += 1;
            }
        });

        writer_index += 1;
        if writer_index >= CONNECTIONS_PER_NODE {
            writer_index = 0;
        }

        {
            let mut logic = logic.lock().unwrap();
            if unconfirmed {
                logic.delayed.confirmed(&hash, now);
                logic.block_factory.confirm(&hash);
            }
            logic.high_prio_tracker.published(hash);
        }
    }
    cancel_token.cancel();
}

async fn republish_delayed_blocks(
    tx_forks: mpsc::Sender<Forks>,
    logic: &Mutex<SpamLogic>,
    cancel_token: CancellationToken,
    clock: &SteadyClock,
) {
    loop {
        while let Some(block) = {
            let now = clock.now();
            logic.lock().unwrap().delayed.next(now)
        } {
            tx_forks.send(Forks::new(block)).await.unwrap();
        }

        if logic.lock().unwrap().delayed.is_finished() {
            break;
        }

        tokio::time::sleep(Duration::from_millis(100)).await;
    }
    cancel_token.cancel();
}

async fn receive_messages(
    mut readers: Vec<Vec<ReadHalf<TcpStream>>>,
    _protocol: ProtocolInfo,
    cancel_token: CancellationToken,
) {
    select! {
        _ = cancel_token.cancelled() => {},
        _ = async {
            let mut set = JoinSet::new();
            for mut reader in readers.drain(..).flatten() {
                set.spawn(async move {
                    let mut recv_buffer = vec![0; 1024 * 4];
                    loop{
                        let _ = reader.read(&mut recv_buffer).await.unwrap();
                    }
                });
            }
            set.join_all().await;
        } => {}
    }
}

async fn log_status(
    logic: &Mutex<SpamLogic>,
    clock: &SteadyClock,
    cancel_token: CancellationToken,
) {
    while let Err(_) = timeout(Duration::from_secs(1), cancel_token.cancelled()).await {
        let now = clock.now();
        let mut logic = logic.lock().unwrap();
        let cps = logic.cps(now);
        let avg_conf_time = logic.average_conf_time().as_millis();
        let bps = logic.current_bps;
        let total = logic.total;
        logic.reset_cps_counter(now);
        drop(logic);

        info!(
            "Confirmed {} blocks | {} bps | {} cps | avg conf time: {avg_conf_time} ms",
            total.to_formatted_string(&Locale::en),
            bps.to_formatted_string(&Locale::en),
            cps.to_formatted_string(&Locale::en),
        );
    }
}
