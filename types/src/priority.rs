use crate::{Amount, UnixMillisTimestamp};

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
    pub const MIN: BlockPriority = BlockPriority::new(Amount::ZERO, TimePriority::MIN);

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
