mod peer;
mod stream;

use std::time::{Duration, SystemTime, UNIX_EPOCH};

pub use peer::*;
pub use stream::*;

use crate::{Amount, UnixMillisTimestamp};

pub trait Serialize {
    fn serialize(&self, stream: &mut dyn BufferWriter);
}

pub trait FixedSizeSerialize: Serialize {
    fn serialized_size() -> usize;
}

pub trait Deserialize {
    type Target;
    fn deserialize(stream: &mut dyn Stream) -> anyhow::Result<Self::Target>;
}

impl Serialize for u64 {
    fn serialize(&self, stream: &mut dyn BufferWriter) {
        stream.write_u64_be_safe(*self)
    }
}

impl FixedSizeSerialize for u64 {
    fn serialized_size() -> usize {
        std::mem::size_of::<u64>()
    }
}

impl Deserialize for u64 {
    type Target = Self;
    fn deserialize(stream: &mut dyn Stream) -> anyhow::Result<u64> {
        stream.read_u64_be()
    }
}

impl Serialize for [u8; 64] {
    fn serialize(&self, stream: &mut dyn BufferWriter) {
        stream.write_bytes_safe(self)
    }
}

impl FixedSizeSerialize for [u8; 64] {
    fn serialized_size() -> usize {
        64
    }
}

impl Deserialize for [u8; 64] {
    type Target = Self;

    fn deserialize(stream: &mut dyn Stream) -> anyhow::Result<Self::Target> {
        let mut buffer = [0; 64];
        stream.read_bytes(&mut buffer, 64)?;
        Ok(buffer)
    }
}

pub fn system_time_as_seconds(time: SystemTime) -> u64 {
    time.duration_since(SystemTime::UNIX_EPOCH)
        .expect("Time went backwards")
        .as_secs()
}

pub fn get_env_or_default_string(variable_name: &str, default: impl Into<String>) -> String {
    std::env::var(variable_name).unwrap_or_else(|_| default.into())
}

pub fn new_test_timestamp() -> SystemTime {
    UNIX_EPOCH + Duration::from_secs(1_000_000)
}

/// Lower timestamps have a higher priority
#[derive(PartialEq, Eq, Copy, Clone, Hash)]
pub struct TimePriority(UnixMillisTimestamp);

impl TimePriority {
    // highest timestamp means lowest priority!
    pub const MIN: TimePriority = TimePriority::new(u64::MAX);

    pub const fn new(timestamp: u64) -> Self {
        Self(UnixMillisTimestamp::new(timestamp))
    }
}

impl Default for TimePriority {
    fn default() -> Self {
        Self::MIN
    }
}

impl Ord for TimePriority {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        other.0.cmp(&self.0)
    }
}

impl PartialOrd for TimePriority {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl std::fmt::Debug for TimePriority {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.0.fmt(f)
    }
}

impl From<UnixMillisTimestamp> for TimePriority {
    fn from(value: UnixMillisTimestamp) -> Self {
        Self(value)
    }
}

impl From<TimePriority> for UnixMillisTimestamp {
    fn from(value: TimePriority) -> Self {
        value.0
    }
}

#[derive(PartialEq, Eq, Copy, Clone, Default, PartialOrd, Ord, Debug, Hash)]
pub struct BlockPriority {
    pub balance: Amount,
    pub time: TimePriority,
}

impl BlockPriority {
    pub const MIN: BlockPriority = BlockPriority::new(Amount::zero(), TimePriority::MIN);

    pub const fn new(balance: Amount, time: TimePriority) -> Self {
        Self { balance, time }
    }

    pub fn new_test_instance() -> Self {
        Self::new(Amount::nano(1), TimePriority::new(42))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn block_priority_order() {
        let a = BlockPriority::new(Amount::from(100), TimePriority::new(5));
        let b = BlockPriority::new(Amount::from(100), TimePriority::new(6));
        let c = BlockPriority::new(Amount::from(101), TimePriority::new(4));
        assert!(a > b);
        assert!(c > a);
    }
}
