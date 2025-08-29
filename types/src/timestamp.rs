use chrono::{DateTime, TimeZone, Utc};
use std::{
    ops::{Add, Mul},
    time::{Duration, SystemTime, SystemTimeError, UNIX_EPOCH},
};

/// Elapsed seconds since UNIX_EPOCH
#[derive(PartialEq, Eq, Clone, Copy, PartialOrd, Ord, Default, Hash)]
pub struct UnixTimestamp(u64);

impl UnixTimestamp {
    pub const ZERO: Self = Self(0);
    pub const MAX: Self = Self(u64::MAX);

    pub const fn new(seconds_since_epoch: u64) -> Self {
        Self(seconds_since_epoch)
    }

    pub const fn new_test_instance() -> Self {
        Self::new(1740000000)
    }

    pub fn now() -> Self {
        Self(Self::seconds_since_unix_epoch())
    }

    pub const fn as_u64(&self) -> u64 {
        self.0
    }

    fn seconds_since_unix_epoch() -> u64 {
        SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .unwrap()
            .as_secs()
    }

    pub fn to_be_bytes(&self) -> [u8; 8] {
        self.0.to_be_bytes()
    }

    pub fn from_be_bytes(bytes: [u8; 8]) -> Self {
        Self(u64::from_be_bytes(bytes))
    }

    pub fn add(&self, seconds: u64) -> Self {
        Self(self.0 + seconds)
    }

    pub fn utc(&self) -> DateTime<Utc> {
        Utc.timestamp_opt(self.0 as i64, 0)
            .latest()
            .unwrap_or_default()
    }
}

impl From<u64> for UnixTimestamp {
    fn from(value: u64) -> Self {
        Self::new(value)
    }
}

impl Add<Duration> for UnixTimestamp {
    type Output = UnixTimestamp;

    fn add(self, rhs: Duration) -> Self::Output {
        Self(self.0 + rhs.as_secs())
    }
}

impl Mul<u64> for UnixTimestamp {
    type Output = UnixTimestamp;

    fn mul(self, rhs: u64) -> Self::Output {
        UnixTimestamp::new(self.0 * rhs)
    }
}

impl TryFrom<SystemTime> for UnixTimestamp {
    type Error = SystemTimeError;

    fn try_from(value: SystemTime) -> Result<Self, Self::Error> {
        Ok(Self(value.duration_since(UNIX_EPOCH)?.as_secs()))
    }
}

impl std::fmt::Display for UnixTimestamp {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Display::fmt(&self.0, f)
    }
}

impl std::fmt::Debug for UnixTimestamp {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Debug::fmt(&self.0, f)
    }
}

/// Elapsed milliseconds since UNIX_EPOCH
#[derive(PartialEq, Eq, Clone, Copy, PartialOrd, Ord, Default, Hash)]
pub struct UnixMillisTimestamp(u64);

impl UnixMillisTimestamp {
    pub const ZERO: Self = Self(0);
    pub const MAX: Self = Self(u64::MAX);

    pub const fn new(millis_since_epoch: u64) -> Self {
        Self(millis_since_epoch)
    }

    pub const fn new_test_instance() -> Self {
        Self::new(1740000000000)
    }

    pub fn now() -> Self {
        Self(milliseconds_since_epoch())
    }

    pub const fn as_u64(&self) -> u64 {
        self.0
    }

    pub fn to_be_bytes(&self) -> [u8; 8] {
        self.0.to_be_bytes()
    }

    pub fn from_be_bytes(bytes: [u8; 8]) -> Self {
        Self(u64::from_be_bytes(bytes))
    }

    pub fn checked_add(&self, duration: Duration) -> Option<Self> {
        self.0.checked_add(duration.as_millis() as u64).map(Self)
    }

    pub fn elapsed(&self, now: UnixMillisTimestamp) -> Duration {
        Duration::from_millis(now.0.saturating_sub(self.0))
    }

    pub fn utc(&self) -> DateTime<Utc> {
        let seconds = (self.0 / 1000) as i64;
        let millis = (self.0 % 1000) as u32;
        let nanos = millis * 1000 * 1000;
        Utc.timestamp_opt(seconds, nanos)
            .latest()
            .unwrap_or_default()
    }
}

impl From<u64> for UnixMillisTimestamp {
    fn from(value: u64) -> Self {
        Self::new(value)
    }
}

impl From<UnixMillisTimestamp> for UnixTimestamp {
    fn from(value: UnixMillisTimestamp) -> Self {
        Self::new(value.0 / 1000)
    }
}

impl From<UnixTimestamp> for UnixMillisTimestamp {
    fn from(value: UnixTimestamp) -> Self {
        Self::new(value.0 * 1000)
    }
}

impl Add<Duration> for UnixMillisTimestamp {
    type Output = UnixMillisTimestamp;

    fn add(self, rhs: Duration) -> Self::Output {
        Self(self.0 + rhs.as_millis() as u64)
    }
}

impl Mul<u64> for UnixMillisTimestamp {
    type Output = UnixMillisTimestamp;

    fn mul(self, rhs: u64) -> Self::Output {
        UnixMillisTimestamp::new(self.0 * rhs)
    }
}

impl std::fmt::Display for UnixMillisTimestamp {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Display::fmt(&self.0, f)
    }
}

impl std::fmt::Debug for UnixMillisTimestamp {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Debug::fmt(&self.0, f)
    }
}

pub fn milliseconds_since_epoch() -> u64 {
    SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .unwrap()
        .as_millis() as u64
}
