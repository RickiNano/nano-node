use super::Tickable;
use crate::{
    thread_factory::{JoinHandle, ThreadFactory},
    thread_pool::ThreadPool,
    CancellationToken,
};
use std::{sync::Arc, time::Duration};

pub struct TickerPool {
    thread_factory: Arc<ThreadFactory>,
    tickers: Vec<(Box<dyn Tickable>, Duration)>,
    cancel_token: CancellationToken,
    main_thread: Option<JoinHandle>,
    workers: Arc<ThreadPool>,
}

impl TickerPool {
    pub fn new(
        workers: Arc<ThreadPool>,
        thread_factory: Arc<ThreadFactory>,
        cancel_token: CancellationToken,
    ) -> Self {
        Self {
            thread_factory,
            tickers: Default::default(),
            cancel_token,
            main_thread: None,
            workers,
        }
    }

    pub fn insert(&mut self, ticker: impl Tickable + 'static, interval: Duration) {
        self.tickers.push((Box::new(ticker), interval));
    }

    pub fn start(&mut self) {
        let mut tickers_copy = Vec::new();
        std::mem::swap(&mut tickers_copy, &mut self.tickers);

        let cancel_token = self.cancel_token.clone();

        self.main_thread = Some(self.thread_factory.spawn("Ticker pool", move || {
            run_tickers(tickers_copy, cancel_token);
        }));
    }

    pub fn stop(&mut self) {
        self.cancel_token.cancel();
        if let Some(handle) = self.main_thread.take() {
            handle.join().expect("Main ticker thread should not fail");
        }
    }
}

fn run_tickers(
    mut tickers: Vec<(Box<dyn Tickable + 'static>, Duration)>,
    cancel_token: CancellationToken,
) {
    while !cancel_token.wait_for_cancellation(Duration::from_millis(100)) {
        for (ticker, interval) in &mut tickers {
            ticker.tick(&cancel_token);
        }
    }
}

impl Drop for TickerPool {
    fn drop(&mut self) {
        self.stop();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{thread_pool::ThreadPool, CancellationToken};
    use std::sync::atomic::{AtomicUsize, Ordering};

    #[test]
    fn spawn_main_thread() {
        let thread_factory = Arc::new(ThreadFactory::new_null());
        let spawn_tracker = thread_factory.track_spawns();
        let thread_pool = Arc::new(ThreadPool::new_null());
        let cancel_token = CancellationToken::new_null_with_uncancelled_waits(1);
        let wait_tracker = cancel_token.track_waits();
        let mut ticker_pool = TickerPool::new(thread_pool.clone(), thread_factory, cancel_token);

        let ticker = TestTicker::new();
        let call_count = ticker.call_count.clone();
        ticker_pool.insert(ticker, Duration::from_millis(1));
        ticker_pool.start();

        let spawns = spawn_tracker.output();
        assert_eq!(spawns.len(), 1);
        assert_eq!(spawns[0].thread_name, "Ticker pool");
        spawns[0].run();
        assert_eq!(call_count.load(Ordering::SeqCst), 1);
        assert_eq!(wait_tracker.output().len(), 2);
    }

    struct TestTicker {
        call_count: Arc<AtomicUsize>,
    }

    impl TestTicker {
        fn new() -> Self {
            Self {
                call_count: Arc::new(AtomicUsize::new(0)),
            }
        }
    }

    impl Tickable for TestTicker {
        fn tick(&mut self, cancel_token: &CancellationToken) {
            self.call_count.fetch_add(1, Ordering::SeqCst);
            cancel_token.cancel();
        }
    }
}
