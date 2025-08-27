use std::{
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, AtomicUsize, Ordering},
    },
    time::Duration,
};

use tokio::{select, task::JoinSet, time::timeout};
use tokio_util::sync::CancellationToken;
use tracing::warn;

use rsnano_nullable_http_client::Url;
use rsnano_output_tracker::{OutputListenerMt, OutputTrackerMt};
use rsnano_types::{Peer, Root, WorkNonce, WorkRequest, WorkRequestAsync};
use rsnano_utils::container_info::{ContainerInfo, ContainerInfoProvider};
use rsnano_work::{WorkPool, WorkPoolBuilder};

use super::distributed_work_client::DistributedWorkClient;

static NEXT_ID: AtomicUsize = AtomicUsize::new(1);

pub struct WorkFactory {
    work_peers: Mutex<Vec<Peer>>,
    cancel_listener: OutputListenerMt<Root>,
    stopped: AtomicBool,
    factory_impl: Arc<WorkFactoryImpl>,
    runtime: Option<tokio::runtime::Handle>,
}

impl WorkFactory {
    fn new(
        work_pool: WorkPool,
        work_client: DistributedWorkClient,
        work_peers: Vec<Peer>,
        timeout: Duration,
        runtime: Option<tokio::runtime::Handle>,
    ) -> Self {
        Self {
            work_peers: Mutex::new(work_peers),
            cancel_listener: OutputListenerMt::new(),
            stopped: AtomicBool::new(false),
            runtime,
            factory_impl: Arc::new(WorkFactoryImpl {
                work_client: work_client.into(),
                running: Mutex::new(Vec::new()),
                local_work_pool: work_pool,
                timeout,
                requests_made: Arc::new(AtomicUsize::new(0)),
            }),
        }
    }

    pub fn disabled() -> Self {
        let builder = WorkFactoryBuilder {
            local_work_pool: None,
            work_peers: Vec::new(),
            runtime: None,
            timeout: DEFAULT_TIMEOUT,
        };
        builder.local_work_pool(|p| p.disabled()).finish()
    }

    pub fn builder(runtime: tokio::runtime::Handle) -> WorkFactoryBuilder {
        WorkFactoryBuilder {
            local_work_pool: None,
            work_peers: Vec::new(),
            runtime: Some(runtime),
            timeout: DEFAULT_TIMEOUT,
        }
    }

    pub fn work_threads(&self) -> usize {
        self.factory_impl.local_work_pool.thread_count()
    }

    pub fn has_opencl(&self) -> bool {
        self.factory_impl.local_work_pool.has_opencl()
    }

    pub fn work_generation_enabled(&self) -> bool {
        self.factory_impl.local_work_pool.work_generation_enabled()
            || !self.work_peers.lock().unwrap().is_empty()
    }

    pub fn stop(&self) {
        self.stopped.store(true, Ordering::SeqCst);
        self.factory_impl.stop();
    }

    pub fn track_cancellations(&self) -> Arc<OutputTrackerMt<Root>> {
        self.cancel_listener.track()
    }

    pub fn requests_made(&self) -> usize {
        self.factory_impl.requests_made.load(Ordering::SeqCst)
    }

    pub fn peers(&self) -> Vec<Peer> {
        self.work_peers.lock().unwrap().clone()
    }

    pub fn add_peer(&self, peer: Peer) {
        self.work_peers.lock().unwrap().push(peer);
    }

    pub fn clear_peers(&self) {
        self.work_peers.lock().unwrap().clear();
    }

    pub fn generate_work(&self, request: WorkRequest) -> Option<WorkNonce> {
        let (req_async, done) = request.into_async();
        self.generate_work_async(req_async);
        done.wait()
    }

    pub fn generate_work_async(&self, request: WorkRequestAsync) {
        if self.stopped.load(Ordering::SeqCst) {
            request.cancelled();
            return;
        }

        let peers = self.work_peers.lock().unwrap().clone();

        if peers.is_empty() {
            self.factory_impl.generate_local(request);
        } else {
            let factory_impl = self.factory_impl.clone();
            self.runtime
                .as_ref()
                .unwrap()
                .spawn(async move { factory_impl.generate_remote_or_local(peers, request).await });
        }
    }

    pub fn cancel(&self, root: Root) {
        self.cancel_listener.emit(root);
        self.factory_impl.cancel(root);
    }
}

impl ContainerInfoProvider for WorkFactory {
    fn container_info(&self) -> ContainerInfo {
        self.factory_impl.local_work_pool.container_info()
    }
}

pub struct WorkFactoryBuilder {
    local_work_pool: Option<WorkPool>,
    work_peers: Vec<Peer>,
    timeout: Duration,
    runtime: Option<tokio::runtime::Handle>,
}

impl WorkFactoryBuilder {
    pub fn local_work_pool(mut self, f: impl FnOnce(WorkPoolBuilder) -> WorkPoolBuilder) -> Self {
        self.local_work_pool = Some(f(WorkPool::builder()).finish());
        self
    }

    pub fn work_peers(mut self, peers: Vec<Peer>) -> Self {
        self.work_peers = peers;
        self
    }

    pub fn finish(self) -> WorkFactory {
        let local_work_pool = self.local_work_pool.unwrap_or_default();
        WorkFactory::new(
            local_work_pool,
            DistributedWorkClient::default(),
            self.work_peers,
            self.timeout,
            self.runtime,
        )
    }
}

const DEFAULT_TIMEOUT: Duration = Duration::from_secs(30);

struct WorkFactoryImpl {
    local_work_pool: WorkPool,
    running: Mutex<Vec<(usize, Root, CancellationToken)>>,
    timeout: Duration,
    requests_made: Arc<AtomicUsize>,
    work_client: Arc<DistributedWorkClient>,
}

impl WorkFactoryImpl {
    fn create_cancellation_token(&self, root: Root) -> (usize, CancellationToken) {
        let cancel_token = CancellationToken::new();

        let id = NEXT_ID.fetch_add(1, Ordering::SeqCst);

        self.running
            .lock()
            .unwrap()
            .push((id, root, cancel_token.clone()));

        (id, cancel_token)
    }

    fn generate_local(&self, request: WorkRequestAsync) {
        if !self.local_work_pool.work_generation_enabled() {
            warn!("Local work generation is disabled!");
            request.cancelled();
        } else {
            self.local_work_pool.generate_async(request);
        }
    }

    async fn generate_remote_or_local(&self, peers: Vec<Peer>, request: WorkRequestAsync) {
        let (id, cancel_token) = self.create_cancellation_token(request.root);

        let result = self
            .generate_remote(peers, request.request(), cancel_token.clone())
            .await;

        self.remove_cancellation_token(id);

        match result {
            None => {
                if cancel_token.is_cancelled() {
                    request.cancelled();
                } else {
                    // No peer returned a result. Fall back to local work generation
                    self.generate_local(request);
                }
            }
            Some(work) => {
                if request.is_valid_work(work) {
                    request.work_found(work);
                } else {
                    warn!("Peer returned invalid work!");
                    request.cancelled();
                }
            }
        }
    }

    async fn generate_remote(
        &self,
        peers: Vec<Peer>,
        request: WorkRequest,
        cancel_token: CancellationToken,
    ) -> Option<WorkNonce> {
        let mut set = JoinSet::<Option<WorkNonce>>::new();

        // Query all configured peers
        for peer in peers {
            let peer_task = PeerWorkTask {
                peer,
                request: request.clone(),
                work_client: self.work_client.clone(),
                requests_made: self.requests_made.clone(),
                timeout: self.timeout,
            };

            let cancel_token = cancel_token.clone();

            set.spawn(async move {
                select! {
                    work = async {
                            let work = peer_task.generate_on_peer().await;
                            if work.is_some(){
                                // We have a valid result. Cancel all other running queries
                                cancel_token.cancel();
                            }
                            work
                        } => work ,
                    _ = cancel_token.cancelled() => None
                }
            });
        }

        while let Some(result) = set.join_next().await {
            if let Ok(Some(work)) = result {
                return Some(work);
            }
        }

        None
    }

    fn remove_cancellation_token(&self, id: usize) {
        self.running.lock().unwrap().retain(|(i, _, _)| *i != id);
    }

    pub fn cancel(&self, root: Root) {
        self.local_work_pool.cancel(&root);
        {
            let to_cancel: Vec<_> = self
                .running
                .lock()
                .unwrap()
                .iter()
                .filter_map(|(_, r, ct)| if *r == root { Some(ct.clone()) } else { None })
                .collect();

            for cancel_token in to_cancel {
                cancel_token.cancel();
            }
        }
    }

    fn stop(&self) {
        let cancel_tokens: Vec<_> = self
            .running
            .lock()
            .unwrap()
            .iter()
            .map(|(_, _, ct)| ct.clone())
            .collect();
        for ct in cancel_tokens {
            ct.cancel();
        }
    }
}

struct PeerWorkTask {
    peer: Peer,
    request: WorkRequest,
    work_client: Arc<DistributedWorkClient>,
    requests_made: Arc<AtomicUsize>,
    timeout: Duration,
}

impl PeerWorkTask {
    pub async fn generate_on_peer(&self) -> Option<WorkNonce> {
        let Some(url) = Self::work_peer_url(&self.peer) else {
            warn!("Invalid work peer: \"{}\"", self.peer);
            return None;
        };

        self.requests_made.fetch_add(1, Ordering::SeqCst);

        let result = timeout(
            self.timeout,
            self.work_client
                .generate_work(url.clone(), self.request.clone()),
        )
        .await;

        match result {
            Ok(Ok(work)) => Some(work),
            Ok(Err(e)) => {
                warn!("Work peer returned error: {:?}", e);
                None
            }
            Err(_) => {
                warn!(
                    "Work peer timed out after {} ms: \"{}\"",
                    self.timeout.as_millis(),
                    url.to_string()
                );
                None
            }
        }
    }

    fn work_peer_url(peer: &Peer) -> Option<Url> {
        if peer.address.starts_with("::") {
            Url::parse(&format!("http://[{}]:{}", peer.address, peer.port)).ok()
        } else {
            Url::parse(&format!("http://{}:{}", peer.address, peer.port)).ok()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        tokio_runner::TokioRunner,
        work::distributed_work_client::{ConfiguredWorkResponse, DistributedWorkClient},
    };
    use rsnano_nullable_http_client::Url;
    use rsnano_types::Peer;
    use std::{thread, time::Duration};
    use tracing_test::traced_test;

    #[test]
    fn use_local_work_pool_when_no_peers_given() {
        let expected_work = WorkNonce::from(12345);
        let (work_factory, _rt) = create_work_factory(TestContext {
            work_pool: WorkPool::new_null(expected_work),
            ..Default::default()
        });
        let request = WorkRequest::new_test_instance();

        let work = work_factory.generate_work(request.clone());

        assert_eq!(work, Some(expected_work));
    }

    #[test]
    fn cancellations_can_be_tracked() {
        let (work_factory, _rt) = create_work_factory(TestContext {
            work_pool: WorkPool::new_null(1.into()),
            ..Default::default()
        });
        let cancel_tracker = work_factory.track_cancellations();

        let root = Root::from(1);
        work_factory.cancel(root);

        assert_eq!(cancel_tracker.output(), vec![root]);
    }

    #[test]
    #[traced_test]
    fn work_generation_disabled() {
        let work_factory = WorkFactory::disabled();
        let result = work_factory.generate_work(WorkRequest::new_test_instance());
        assert_eq!(result, None);
        assert!(logs_contain("Local work generation is disabled!"))
    }

    #[test]
    fn use_remote_work_server() {
        let expected_work = WorkNonce::new(42);
        let work_client = DistributedWorkClient::new_null_with(expected_work);
        let request_tracker = work_client.track_requests();
        let (work_factory, _rt) = create_work_factory(TestContext {
            work_pool: WorkPool::disabled(),
            work_client,
            work_peers: vec![Peer::new("foo.com", 123)],
            ..Default::default()
        });

        let request = WorkRequest::new_test_instance();
        let work = work_factory.generate_work(request.clone());

        assert_eq!(work, Some(expected_work));
        let output = request_tracker.output();
        assert_eq!(output.len(), 1, "no request sent");
        assert_eq!(
            output[0],
            (Url::parse("http://foo.com:123").unwrap(), request)
        );
    }

    #[test]
    fn when_peer_is_incorrect_should_use_local_work_pool() {
        let expected_work = WorkNonce::new(100);

        let (work_factory, _rt) = create_work_factory(TestContext {
            work_peers: vec![Peer::new("invalid peer", 123)],
            work_pool: WorkPool::new_null(expected_work),
            ..Default::default()
        });

        let result = work_factory.generate_work(WorkRequest::new_test_instance());
        assert_eq!(result, Some(expected_work));
    }

    #[test]
    fn when_work_server_returns_error_use_local_work_pool() {
        let expected_work = WorkNonce::new(100);

        let (work_factory, _rt) = create_work_factory(TestContext {
            work_peers: vec![Peer::new("127.0.0.1", 123)],
            work_pool: WorkPool::new_null(expected_work),
            work_client: DistributedWorkClient::new_failing_null("an error"),
            ..Default::default()
        });

        let result = work_factory.generate_work(WorkRequest::new_test_instance());
        assert_eq!(result, Some(expected_work));
    }

    #[test]
    fn when_timed_out_should_use_local_work_pool() {
        let expected_work = WorkNonce::new(100);

        let (work_factory, _rt) = create_work_factory(TestContext {
            work_peers: vec![Peer::new("127.0.0.1", 123)],
            work_pool: WorkPool::new_null(expected_work),
            work_client: DistributedWorkClient::new_halting_null(),
            timeout: Duration::ZERO,
            ..Default::default()
        });

        let result = work_factory.generate_work(WorkRequest::new_test_instance());

        assert_eq!(result, Some(expected_work));
    }

    #[test]
    fn calls_multiple_peers_and_uses_first_ok_result() {
        let expected_work = WorkNonce::new(100);
        let peer1 = Peer::new("127.0.0.1", 123);
        let peer2 = Peer::new("127.0.0.1", 456);
        let peer3 = Peer::new("127.0.0.1", 789);

        let (work_factory, _rt) = create_work_factory(TestContext {
            work_peers: vec![peer1, peer2, peer3],
            work_pool: WorkPool::disabled(),
            work_client: DistributedWorkClient::null_builder()
                .response(
                    "http://127.0.0.1:123",
                    ConfiguredWorkResponse::Error("failed".to_string()),
                )
                .response("http://127.0.0.1:456", ConfiguredWorkResponse::Halt)
                .response(
                    "http://127.0.0.1:789",
                    ConfiguredWorkResponse::Ok(expected_work),
                )
                .finish(),
            ..Default::default()
        });

        let result = work_factory.generate_work(WorkRequest::new_test_instance());

        assert_eq!(result, Some(expected_work));
    }

    #[test]
    fn cancel() {
        let (work_factory, _rt) = create_work_factory(TestContext {
            work_peers: vec![Peer::new("127.0.0.1", 123)],
            work_client: DistributedWorkClient::new_halting_null(),
            ..Default::default()
        });

        let request = WorkRequest::new_test_instance();

        let mut result = Some(WorkNonce::new(1000));
        std::thread::scope(|scope| {
            scope.spawn(|| result = work_factory.generate_work(request.clone()));
            while work_factory.requests_made() == 0 {
                thread::yield_now();
            }
            work_factory.cancel(request.root);
        });
        assert_eq!(result, None);
    }

    #[test]
    fn when_stopped_should_return_none() {
        let (work_factory, _rt) = create_work_factory(TestContext {
            work_peers: vec![Peer::new("127.0.0.1", 123)],
            work_client: DistributedWorkClient::new_halting_null(),
            ..Default::default()
        });

        work_factory.stop();

        assert_eq!(
            work_factory.generate_work(WorkRequest::new_test_instance()),
            None
        );
    }

    #[test]
    fn cancel_when_stopped() {
        let (work_factory, _rt) = create_work_factory(TestContext {
            work_peers: vec![Peer::new("127.0.0.1", 123)],
            work_client: DistributedWorkClient::new_halting_null(),
            ..Default::default()
        });

        let request = WorkRequest::new_test_instance();

        let mut result = Some(WorkNonce::new(1000));
        std::thread::scope(|scope| {
            scope.spawn(|| result = work_factory.generate_work(request.clone()));
            while work_factory.requests_made() == 0 {
                thread::yield_now();
            }
            work_factory.stop();
        });
        assert_eq!(result, None);
    }

    #[test]
    fn validate_difficulty_of_remote_work() {
        let work_client = DistributedWorkClient::new_null_with(WorkNonce::new(42));
        let (work_factory, _rt) = create_work_factory(TestContext {
            work_pool: WorkPool::disabled(),
            work_client,
            work_peers: vec![Peer::new("foo.com", 123)],
            ..Default::default()
        });

        let request = WorkRequest::new(Root::from(123), u64::MAX);
        let work = work_factory.generate_work(request.clone());

        assert_eq!(work, None);
    }

    struct TestContext {
        work_pool: WorkPool,
        work_client: DistributedWorkClient,
        work_peers: Vec<Peer>,
        timeout: Duration,
    }

    impl Default for TestContext {
        fn default() -> Self {
            Self {
                work_pool: WorkPool::new_null(WorkNonce::new(42)),
                work_client: DistributedWorkClient::new_null_with(WorkNonce::new(43)),
                work_peers: Vec::new(),
                timeout: DEFAULT_TIMEOUT,
            }
        }
    }

    fn create_work_factory(context: TestContext) -> (WorkFactory, TokioRunner) {
        let mut runner = TokioRunner::new(1);
        runner.start();

        let factory = WorkFactory::new(
            context.work_pool,
            context.work_client,
            context.work_peers,
            context.timeout,
            Some(runner.handle().clone()),
        );
        (factory, runner)
    }
}
