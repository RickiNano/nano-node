use rsnano_output_tracker::{OutputListenerMt, OutputTrackerMt};
use std::{
    any::Any,
    sync::{Arc, Mutex},
};

#[derive(Default)]
pub struct ThreadFactory {
    spawn_listener: OutputListenerMt<SpawnEvent>,
    is_nulled: bool,
}

impl ThreadFactory {
    pub fn new_null() -> Self {
        Self {
            spawn_listener: Default::default(),
            is_nulled: true,
        }
    }

    pub fn track_spawns(&self) -> Arc<OutputTrackerMt<SpawnEvent>> {
        self.spawn_listener.track()
    }

    pub fn spawn<F>(&self, name: impl Into<String>, f: F) -> JoinHandle
    where
        F: FnOnce() + Send + 'static,
    {
        let name = name.into();

        if self.is_nulled {
            let callback: Option<Box<dyn FnOnce() + Send>> = Some(Box::new(move || {
                f();
            }));
            self.spawn_listener.emit(SpawnEvent {
                thread_name: name,
                callback: Arc::new(Mutex::new(callback)),
            });
            JoinHandle::new_null()
        } else {
            self.spawn_listener.emit(SpawnEvent {
                thread_name: name.clone(),
                callback: Arc::new(Mutex::new(None)),
            });

            let handle = std::thread::Builder::new()
                .name(name)
                .spawn(f)
                .expect("Should spawn thread");

            JoinHandle {
                strategy: JoinHandleStrategy::Real(handle),
            }
        }
    }
}

pub struct JoinHandle {
    strategy: JoinHandleStrategy,
}

impl JoinHandle {
    pub fn new_null() -> Self {
        Self {
            strategy: JoinHandleStrategy::Nulled,
        }
    }

    pub fn join(self) -> Result<(), Box<dyn Any + Send + 'static>> {
        match self.strategy {
            JoinHandleStrategy::Real(handle) => handle.join(),
            JoinHandleStrategy::Nulled => Ok(()),
        }
    }
}

enum JoinHandleStrategy {
    Real(std::thread::JoinHandle<()>),
    Nulled,
}

#[derive(Clone)]
pub struct SpawnEvent {
    pub thread_name: String,
    callback: Arc<Mutex<Option<Box<dyn FnOnce() + Send>>>>,
}

impl SpawnEvent {
    pub fn run(&self) {
        let Some(callback) = self.callback.lock().unwrap().take() else {
            panic!("No callback to call");
        };
        callback();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{
        sync::{
            Arc,
            atomic::{AtomicUsize, Ordering},
            mpsc,
        },
        time::Duration,
    };

    #[test]
    fn spawns_real_thread() {
        let thread_factory = ThreadFactory::default();
        let (tx, rx) = mpsc::channel::<usize>();
        let handle = thread_factory.spawn("Test thread", move || tx.send(123).unwrap());
        rx.recv_timeout(Duration::from_secs(2))
            .expect("Should receive something from thread");
        handle.join().unwrap();
    }

    #[test]
    fn can_be_nulled() {
        let thread_factory = ThreadFactory::new_null();
        let spawn_tracker = thread_factory.track_spawns();
        let called = Arc::new(AtomicUsize::new(0));
        let called2 = called.clone();
        thread_factory.spawn("foobar", move || {
            called2.fetch_add(1, Ordering::SeqCst);
        });
        let spawned = spawn_tracker.output();
        assert_eq!(spawned.len(), 1, "Spawn events");
        assert_eq!(spawned[0].thread_name, "foobar");
        assert_eq!(called.load(Ordering::SeqCst), 0);

        spawned[0].run();
        assert_eq!(called.load(Ordering::SeqCst), 1);
    }
}
