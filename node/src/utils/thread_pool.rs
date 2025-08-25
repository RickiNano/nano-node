use std::sync::{Arc, Mutex};

pub struct ThreadPool {
    data: Arc<Mutex<Option<ThreadPoolData>>>,
    stopped: Arc<Mutex<bool>>,
}

impl ThreadPool {
    pub fn create(num_threads: usize, thread_name: impl Into<String>) -> Self {
        Self::new(num_threads, thread_name.into())
    }

    pub fn new_null() -> Self {
        Self::new(1, "nulled thread pool".to_string())
    }

    fn new(num_threads: usize, thread_name: String) -> Self {
        Self {
            stopped: Arc::new(Mutex::new(false)),
            data: Arc::new(Mutex::new(Some(ThreadPoolData {
                pool: threadpool::Builder::new()
                    .num_threads(num_threads)
                    .thread_name(thread_name)
                    .build(),
            }))),
        }
    }

    pub fn execute(&self, callback: Box<dyn FnOnce() + Send>) {
        let stopped_guard = self.stopped.lock().unwrap();
        if !*stopped_guard {
            let data_guard = self.data.lock().unwrap();
            drop(stopped_guard);
            if let Some(data) = data_guard.as_ref() {
                data.execute(callback);
            }
        }
    }

    pub fn join(&self) {
        let mut stopped_guard = self.stopped.lock().unwrap();
        if !*stopped_guard {
            let mut data_guard = self.data.lock().unwrap();
            *stopped_guard = true;
            drop(stopped_guard);
            if let Some(data) = data_guard.take() {
                drop(data_guard);
                data.pool.join();
            }
        }
    }

    pub fn num_queued_tasks(&self) -> usize {
        self.data
            .lock()
            .unwrap()
            .as_ref()
            .map(|i| i.pool.queued_count())
            .unwrap_or_default()
    }
}

impl Drop for ThreadPool {
    fn drop(&mut self) {
        self.join()
    }
}

struct ThreadPoolData {
    pool: threadpool::ThreadPool,
}

impl ThreadPoolData {
    fn execute(&self, callback: Box<dyn FnOnce() + Send>) {
        self.pool.execute(callback);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[test]
    fn push_task() {
        let (tx, rx) = std::sync::mpsc::channel();
        let pool = ThreadPool::create(1, "test thread".to_string());
        pool.execute(Box::new(move || {
            tx.send("foo").unwrap();
        }));
        let result = rx.recv_timeout(Duration::from_millis(300));
        assert_eq!(result, Ok("foo"));
    }
}
