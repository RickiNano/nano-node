pub struct ThreadPool {
    strategy: Strategy,
}

impl ThreadPool {
    pub fn new(num_threads: usize, thread_name: impl Into<String>) -> Self {
        Self {
            strategy: Strategy::Real(
                threadpool::Builder::new()
                    .num_threads(num_threads)
                    .thread_name(thread_name.into())
                    .build(),
            ),
        }
    }

    pub fn new_null() -> Self {
        Self {
            strategy: Strategy::Nulled,
        }
    }

    pub fn execute<F>(&self, callback: F)
    where
        F: FnOnce() + Send + 'static,
    {
        match &self.strategy {
            Strategy::Real(pool) => pool.execute(callback),
            Strategy::Nulled => {}
        }
    }

    pub fn join(&self) {
        match &self.strategy {
            Strategy::Real(pool) => pool.join(),
            Strategy::Nulled => {}
        }
    }

    pub fn queued_count(&self) -> usize {
        match &self.strategy {
            Strategy::Real(pool) => pool.queued_count(),
            Strategy::Nulled => 0,
        }
    }
}

impl Drop for ThreadPool {
    fn drop(&mut self) {
        self.join()
    }
}

enum Strategy {
    Real(threadpool::ThreadPool),
    Nulled,
}

#[cfg(test)]
mod tests {
    use super::*;
    use ntest::assert_false;
    use std::{
        sync::{
            atomic::{AtomicBool, Ordering},
            Arc,
        },
        time::Duration,
    };

    #[test]
    fn execute_task() {
        let (tx, rx) = std::sync::mpsc::channel();
        let pool = ThreadPool::new(1, "test thread".to_string());
        pool.execute(Box::new(move || {
            tx.send("foo").unwrap();
        }));
        let result = rx.recv_timeout(Duration::from_millis(2000));
        assert_eq!(result, Ok("foo"));
    }

    #[test]
    fn can_be_nulled() {
        let pool = ThreadPool::new_null();
        let called = Arc::new(AtomicBool::new(false));
        let called2 = called.clone();
        pool.execute(move || called2.store(true, Ordering::SeqCst));
        assert_eq!(pool.queued_count(), 0);
        assert_false!(called.load(Ordering::SeqCst));
    }
}
