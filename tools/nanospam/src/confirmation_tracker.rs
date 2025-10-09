use std::sync::{Mutex, mpsc::Receiver};

use rsnano_types::BlockHash;
use rsnano_websocket_messages::{BlockConfirmed, MessageEnvelope, Topic};

use crate::domain::spam_logic::SpamLogic;
use rsnano_nullable_clock::Timestamp;

pub(crate) fn track_confirmations(
    rx_ws_msg: Receiver<(MessageEnvelope, Timestamp)>,
    logic: &Mutex<SpamLogic>,
) {
    while let Ok((msg, timestamp)) = rx_ws_msg.recv() {
        if msg.topic == Some(Topic::Confirmation) {
            let data: BlockConfirmed = serde_json::from_value(msg.message.unwrap()).unwrap();
            let block_hash = BlockHash::decode_hex(data.hash).unwrap();

            let mut logic = logic.lock().unwrap();
            logic.confirmed(&block_hash, timestamp);
        }
    }
}
