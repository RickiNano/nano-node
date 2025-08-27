use crate::{
    BlockHash,
    stream::{Deserialize, Stream, StreamExt},
};
use std::io::Write;

#[derive(Default, PartialEq, Eq, Debug, Clone)]
pub struct ConfirmationHeightInfo {
    pub height: u64,
    pub frontier: BlockHash,
}

impl ConfirmationHeightInfo {
    pub fn new(height: u64, frontier: BlockHash) -> Self {
        Self { height, frontier }
    }

    pub fn new_test_instance() -> Self {
        Self {
            height: 42,
            frontier: BlockHash::from(7),
        }
    }

    pub fn to_bytes(&self) -> [u8; 40] {
        let mut buffer = [0; 40];
        self.serialize_writer(&mut buffer.as_mut())
            .expect("Should serialize conf height info");
        buffer
    }

    fn serialize_writer<T>(&self, writer: &mut T) -> std::io::Result<()>
    where
        T: Write,
    {
        writer.write_all(&self.height.to_ne_bytes())?;
        writer.write_all(self.frontier.as_bytes())
    }
}

impl Deserialize for ConfirmationHeightInfo {
    type Target = Self;
    fn deserialize(stream: &mut dyn Stream) -> anyhow::Result<Self> {
        let height = stream.read_u64_ne()?;
        let frontier = BlockHash::deserialize(stream)?;
        Ok(Self { height, frontier })
    }
}
