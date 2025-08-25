mod timer_thread;

use crate::CancellationToken;
pub use timer_thread::{TimerStartEvent, TimerStartType, TimerThread};

pub trait Tickable: Send {
    fn tick(&mut self, cancel_token: &CancellationToken);
}
