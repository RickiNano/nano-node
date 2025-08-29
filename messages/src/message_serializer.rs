use super::{Message, MessageHeader};
use rsnano_types::ProtocolInfo;

#[derive(Clone)]
pub struct MessageSerializer {
    protocol: ProtocolInfo,
    buffer: Vec<u8>,
}

impl MessageSerializer {
    const BUFFER_SIZE: usize = MessageHeader::SERIALIZED_SIZE + Message::MAX_MESSAGE_SIZE;
    pub fn new(protocol: ProtocolInfo) -> Self {
        Self {
            protocol,
            buffer: Vec::with_capacity(Self::BUFFER_SIZE),
        }
    }

    pub fn new_with_buffer_size(protocol: ProtocolInfo, buffer_size: usize) -> Self {
        Self {
            protocol,
            buffer: Vec::with_capacity(buffer_size),
        }
    }

    pub fn serialize(&'_ mut self, message: &Message) -> &'_ [u8] {
        self.buffer.resize(MessageHeader::SERIALIZED_SIZE, 0);
        let payload_len;
        {
            message
                .serialize(&mut self.buffer)
                .expect("Writing message body should succeed");
            payload_len = self.buffer.len() - MessageHeader::SERIALIZED_SIZE;

            let mut header = MessageHeader::new(message.message_type(), self.protocol);
            header.extensions = message.header_extensions(payload_len as u16);
            header
                .serialize(&mut &mut self.buffer[..MessageHeader::SERIALIZED_SIZE])
                .expect("Writing header should succeed");
        }
        &self.buffer[..MessageHeader::SERIALIZED_SIZE + payload_len]
    }
}

impl Default for MessageSerializer {
    fn default() -> Self {
        Self::new(ProtocolInfo::default())
    }
}
