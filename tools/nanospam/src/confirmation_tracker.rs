use std::{
    sync::{
        Mutex,
        atomic::{AtomicUsize, Ordering},
        mpsc::Receiver,
    },
    time::Duration,
};

use num_format::{Locale, ToFormattedString};
use tracing::info;

use rsnano_types::BlockHash;
use rsnano_websocket_messages::{BlockConfirmed, MessageEnvelope, Topic};

use crate::domain::spam_logic::SpamLogic;
use rsnano_nullable_clock::{SteadyClock, Timestamp};

pub(crate) fn track_confirmations(
    rx_ws_msg: Receiver<(MessageEnvelope, Timestamp)>,
    logic: &Mutex<SpamLogic>,
    ws_queue_len: &AtomicUsize,
    clock: &SteadyClock,
) {
    let mut start = clock.now();
    let mut last_log = start;
    while let Ok((msg, timestamp)) = rx_ws_msg.recv() {
        let len = ws_queue_len.fetch_sub(1, Ordering::Relaxed);
        if msg.topic == Some(Topic::Confirmation) {
            let data: BlockConfirmed = serde_json::from_value(msg.message.unwrap()).unwrap();
            let block_hash = BlockHash::decode_hex(data.hash).unwrap();

            let mut logic = logic.lock().unwrap();
            logic.confirmed(&block_hash, timestamp);

            if last_log.elapsed(clock.now()) > Duration::from_secs(1) {
                let cps =
                    (logic.confirmed as f64 / start.elapsed(clock.now()).as_secs_f64()) as i32;
                let avg_conf_time = if logic.confirmed == 0 {
                    0
                } else {
                    logic.sum_conf_time.as_millis() / logic.confirmed as u128
                };
                let bps = logic.current_bps;
                let total = logic.total;
                logic.reset_cps_counter();
                drop(logic);

                info!(
                    "Confirmed {} blocks | {} bps | {} cps | avg conf time: {avg_conf_time} ms | ws queue: {len}",
                    total.to_formatted_string(&Locale::en),
                    bps.to_formatted_string(&Locale::en),
                    cps.to_formatted_string(&Locale::en),
                );
                start = clock.now();
                last_log = clock.now();
            }
        }
    }
}
