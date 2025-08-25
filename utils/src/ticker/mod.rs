use crate::CancellationToken;

pub trait Tickable: Send {
    fn tick(&mut self, cancel_token: &CancellationToken);
}
