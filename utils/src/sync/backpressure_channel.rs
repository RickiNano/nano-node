use std::{
    cmp::max,
    sync::{
        Arc,
        atomic::{AtomicBool, AtomicI32, Ordering},
        mpsc::{self, RecvError, SendError, TryRecvError},
    },
};

/// BackpressureChannel is a wrapper around mpsc::channel that tracks the size of the queue
pub struct Sender<T> {
    sender: mpsc::Sender<T>,
    queue_size: Arc<AtomicI32>,
}

/// BackpressureReceiver is the receiving end of the BackpressureChannel
pub struct Receiver<T> {
    receiver: mpsc::Receiver<T>,
    queue_size: Arc<AtomicI32>,
    soft_limit: usize,
    is_cooling_down: AtomicBool,
}

pub fn channel<T>(soft_limit: usize) -> (Sender<T>, Receiver<T>) {
    let (sender, receiver) = mpsc::channel();
    let queue_size = Arc::new(AtomicI32::new(0));

    let tx = Sender {
        sender,
        queue_size: Arc::clone(&queue_size),
    };

    let rx = Receiver {
        receiver,
        queue_size,
        soft_limit,
        is_cooling_down: AtomicBool::new(false),
    };

    (tx, rx)
}

impl<T> Sender<T> {
    /// Send a value to the channel and track the size
    pub fn send(&self, t: T) -> Result<(), SendError<T>> {
        let result = self.sender.send(t);
        if result.is_ok() {
            self.queue_size.fetch_add(1, Ordering::Relaxed);
        }
        result
    }

    /// Get the current size of the channel
    pub fn len(&self) -> usize {
        let len = self.queue_size.load(Ordering::Relaxed);
        max(len, 0) as usize
    }

    /// Check if the channel is empty
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

impl<T> Receiver<T> {
    /// Receive a value from the channel and update the size
    pub fn recv(&self) -> Result<T, RecvError> {
        let result = self.receiver.recv();
        if result.is_ok() {
            self.received();
        }
        result
    }

    /// Try to receive a value without blocking
    pub fn try_recv(&self) -> Result<T, TryRecvError> {
        let result = self.receiver.try_recv();
        if result.is_ok() {
            self.received();
        }
        result
    }

    fn received(&self) {
        self.queue_size.fetch_sub(1, Ordering::Relaxed);
    }

    /// Get the current size of the channel
    pub fn len(&self) -> usize {
        let len = self.queue_size.load(Ordering::Relaxed);
        max(len, 0) as usize
    }

    /// Check if the channel is empty
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn should_cool_down(&self) -> bool {
        let cooldown = self.is_cooling_down.load(Ordering::Relaxed);
        if cooldown && self.is_recovered() {
            self.is_cooling_down.store(false, Ordering::Relaxed);
        } else if !cooldown && self.len() >= self.soft_limit {
            self.is_cooling_down.store(true, Ordering::Relaxed);
        }
        self.is_cooling_down.load(Ordering::Relaxed)
    }

    fn is_recovered(&self) -> bool {
        self.len() <= self.soft_limit / 2
    }
}

impl<T> Clone for Sender<T> {
    fn clone(&self) -> Self {
        Sender {
            sender: self.sender.clone(),
            queue_size: Arc::clone(&self.queue_size),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;

    #[test]
    fn test_basic_functionality() {
        let (tx, rx) = channel(1000);

        // Initially empty
        assert_eq!(tx.len(), 0);
        assert!(tx.is_empty());

        // Send and check size
        tx.send(42).unwrap();
        assert_eq!(tx.len(), 1);
        assert!(!tx.is_empty());

        // Receive and check size
        let val = rx.recv().unwrap();
        assert_eq!(val, 42);
        assert_eq!(rx.len(), 0);
        assert!(rx.is_empty());
    }

    #[test]
    fn test_never_negative() {
        let (tx, rx) = channel(1000);

        // Send one item
        tx.send(1).unwrap();
        assert_eq!(tx.len(), 1);

        // Receive the item
        rx.recv().unwrap();
        assert_eq!(rx.len(), 0);

        // Try to receive more than sent (should error but not affect counter)
        let result = rx.try_recv();
        assert!(result.is_err());
        assert_eq!(rx.len(), 0); // Counter should still be zero, not negative
    }

    #[test]
    fn test_channel_cloning() {
        let (tx1, rx) = channel(1000);
        let tx2 = tx1.clone();

        // Send from both senders
        tx1.send(1).unwrap();
        tx2.send(2).unwrap();

        assert_eq!(tx1.len(), 2);
        assert_eq!(tx2.len(), 2);

        // Receive both items
        assert_eq!(rx.recv().unwrap(), 1);
        assert_eq!(rx.recv().unwrap(), 2);

        assert_eq!(rx.len(), 0);
    }

    #[test]
    fn test_simple_threading() {
        let (tx, rx) = channel(1000);

        // Get a copy of the counter for testing after rx is moved
        let queue_size = Arc::clone(&rx.queue_size);

        // Spawn thread to send items
        let sender_thread = thread::spawn(move || {
            tx.send(1).unwrap();
            tx.send(2).unwrap();
            tx.send(3).unwrap();
        });

        // Spawn thread to receive items
        let receiver_thread = thread::spawn(move || {
            let sum = rx.recv().unwrap() + rx.recv().unwrap() + rx.recv().unwrap();
            sum
        });

        // Wait for threads to complete
        sender_thread.join().unwrap();
        let sum = receiver_thread.join().unwrap();

        // Verify results
        assert_eq!(sum, 6); // 1 + 2 + 3
        assert_eq!(queue_size.load(Ordering::Relaxed), 0); // Counter should be back to zero
    }

    #[test]
    fn should_know_when_to_cool_down() {
        let (tx, rx) = channel(4);
        tx.send(1).unwrap();
        tx.send(2).unwrap();
        tx.send(3).unwrap();
        assert_eq!(rx.should_cool_down(), false);
        tx.send(4).unwrap();
        assert_eq!(rx.should_cool_down(), true);
    }

    #[test]
    fn should_end_cooldown_when_half_of_soft_limit_reached() {
        let (tx, rx) = channel(4);
        tx.send(1).unwrap();
        tx.send(2).unwrap();
        tx.send(3).unwrap();
        tx.send(4).unwrap();
        assert_eq!(rx.should_cool_down(), true);
        rx.recv().unwrap();
        assert_eq!(rx.should_cool_down(), true);
        rx.recv().unwrap();
        assert_eq!(rx.should_cool_down(), false);
    }
}
