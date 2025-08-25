use std::{collections::VecDeque, sync::Mutex};

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
            strategy: Strategy::Nulled(ThreadPoolStub::default()),
        }
    }

    pub fn execute<F>(&self, callback: F)
    where
        F: FnOnce() + Send + 'static,
    {
        match &self.strategy {
            Strategy::Real(pool) => pool.execute(callback),
            Strategy::Nulled(pool) => pool.enqueue(callback),
        }
    }

    pub fn join(&self) {
        match &self.strategy {
            Strategy::Real(pool) => pool.join(),
            Strategy::Nulled(_) => {}
        }
    }

    pub fn queued_count(&self) -> usize {
        match &self.strategy {
            Strategy::Real(pool) => pool.queued_count(),
            Strategy::Nulled(pool) => pool.queued_count(),
        }
    }

    /// Only available if the pool is nulled! Run the enqueued tasks.
    pub fn simulate(&self) {
        match &self.strategy {
            Strategy::Real(_) => {
                panic!("Simulating executions is only possible with a nulled thread pool!")
            }
            Strategy::Nulled(pool) => pool.execute_queued_tasks(),
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
    Nulled(ThreadPoolStub),
}

struct ThreadPoolStub {
    queue: Mutex<VecDeque<Box<dyn FnOnce() + Send>>>,
}

impl Default for ThreadPoolStub {
    fn default() -> Self {
        Self {
            queue: Mutex::new(VecDeque::new()),
        }
    }
}

impl ThreadPoolStub {
    fn enqueue(&self, task: impl FnOnce() + Send + 'static) {
        self.queue.lock().unwrap().push_back(Box::new(task));
    }

    fn queued_count(&self) -> usize {
        self.queue.lock().unwrap().len()
    }

    fn execute_queued_tasks(&self) {
        loop {
            let Some(task) = self.queue.lock().unwrap().pop_front() else {
                break;
            };
            task();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ntest::assert_false;
    use std::{
        sync::{
            Arc,
            atomic::{AtomicBool, Ordering},
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
    #[should_panic(expected = "Simulating executions is only possible with a nulled thread pool!")]
    fn panic_when_simulate_called_on_real_pool() {
        let pool = ThreadPool::new(1, "test thread".to_string());
        pool.simulate();
    }

    #[test]
    fn can_be_nulled() {
        let pool = ThreadPool::new_null();
        let called = Arc::new(AtomicBool::new(false));
        let called2 = called.clone();

        pool.execute(move || called2.store(true, Ordering::SeqCst));

        assert_eq!(pool.queued_count(), 1);
        assert_false!(called.load(Ordering::SeqCst));

        pool.simulate();

        assert_eq!(pool.queued_count(), 0);
        assert!(called.load(Ordering::SeqCst));
    }
}
