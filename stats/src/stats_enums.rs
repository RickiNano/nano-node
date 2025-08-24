use num_derive::FromPrimitive;
use serde::Serialize;
use serde_variant::to_variant_name;

/// Primary statistics type
#[repr(u8)]
#[derive(FromPrimitive, Serialize, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Debug)]
#[serde(rename_all = "snake_case")]
pub enum StatType {
    Error,
    Message,
    Ledger,
    Rollback,
    Network,
    VoteProcessor,
    VoteProcessorTier,
    VoteProcessorOverfill,
    VoteRebroadcaster,
    Election,
    HttpCallbacks,
    Tcp,
    TcpServer,
    TcpChannels,
    TcpListener,
    TcpListenerRejected,
    ConfirmationHeight,
    ConfirmationObserver,
    ConfirmingSet,
    Drop,
    Aggregator,
    Requests,
    RequestAggregator,
    RequestAggregatorVote,
    RequestAggregatorReplies,
    Filter,
    Telemetry,
    VoteGenerator,
    VoteGeneratorFinal,
    VoteCache,
    VoteCacheProcessor,
    Hinting,
    BlockProcessor,
    BlockProcessorSource,
    BlockProcessorResult,
    BlockProcessorOverfill,
    Bootstrap,
    BootstrapVerify,
    BootstrapVerifyBlocks,
    BootstrapVerifyFrontiers,
    BootstrapProcess,
    BootstrapRequest,
    BootstrapRequestBlocks,
    BootstrapReply,
    BootstrapNext,
    BootstrapFrontiers,
    BootstrapAccountSets,
    BootstrapFrontierScan,
    BootstrapTimeout,
    BootstrapServer,
    BootstrapServerRequest,
    BootstrapServerOverfill,
    BootstrapServerResponse,
    Active,
    ActiveElections,
    ActiveElectionsStarted,
    ActiveElectionsConfirmed,
    ActiveElectionsDropped,
    ActiveElectionsTimeout,
    ActiveElectionsCancelled,
    ActiveTimeout,
    Backlog,
    BoundedBacklog,
    Unchecked,
    ElectionScheduler,
    ElectionBucket,
    OptimisticScheduler,
    Handshake,
    RepCrawler,
    LocalBlockBroadcaster,
    RepTiers,
    SynCookies,
    PeerHistory,
    ProcessConfirmed,
    Pruning,
}

impl StatType {
    pub fn as_str(&self) -> &'static str {
        to_variant_name(self).unwrap_or_default()
    }
}

// Optional detail type
#[repr(u16)]
#[derive(FromPrimitive, Serialize, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug)]
#[serde(rename_all = "snake_case")]
pub enum DetailType {
    // common
    All = 0,
    Ok,
    Loop,
    LoopCleanup,
    Process,
    Processed,
    Ignored,
    Update,
    Updated,
    Inserted,
    Erased,
    Request,
    RequestFailed,
    RequestSuccess,
    Broadcast,
    Cleanup,
    Top,
    None,
    Success,
    Unknown,
    Cache,
    Rebroadcast,
    QueueOverflow,
    Triggered,
    Notify,
    Duplicate,
    Confirmed,
    Unconfirmed,
    Cemented,
    Cooldown,
    Recovered,
    Empty,
    Done,
    Retry,
    Prioritized,
    Pending,
    Sync,
    Requeued,
    Evicted,
    Sent,

    // processing queue
    Queue,
    Overfill,
    Batch,

    // error specific
    InsufficientWork,
    HttpCallback,
    UnreachableHost,
    InvalidNetwork,
    Conflict,

    // confirmation_observer specific
    ActiveQuorum,
    ActiveConfHeight,
    InactiveConfHeight,

    // ledger, block, bootstrap
    Send,
    Receive,
    Open,
    Change,
    StateBlock,
    EpochBlock,
    Fork,
    Old,
    GapPrevious,
    GapSource,
    Rollback,
    RollbackFailed,
    Progress,
    BadSignature,
    NegativeSpend,
    Unreceivable,
    GapEpochOpenPending,
    OpenedBurnAccount,
    BalanceMismatch,
    RepresentativeMismatch,
    BlockPosition,

    // block source
    Live,
    LiveOriginator,
    Bootstrap,
    BootstrapLegacy,
    Unchecked,
    Local,
    Forced,
    Election,

    // message specific
    NotAType,
    Invalid,
    Keepalive,
    Publish,
    ConfirmReq,
    ConfirmAck,
    NodeIdHandshake,
    TelemetryReq,
    TelemetryAck,
    AscPullReq,
    AscPullAck,

    // dropped messages
    ConfirmAckZeroAccount,

    // bootstrap, callback
    Initiate,
    InitiateLegacyAge,
    InitiateLazy,
    InitiateWalletLazy,

    // bootstrap specific
    BulkPull,
    BulkPullAccount,
    BulkPullErrorStartingRequest,
    BulkPullFailedAccount,
    BulkPullRequestFailure,
    BulkPush,
    FrontierReq,
    FrontierConfirmationFailed,
    ErrorSocketClose,

    // vote result
    Vote,

    // election specific
    GenerateVoteNormal,
    GenerateVoteFinal,
    ConfirmationRequest,

    // election types
    Manual,
    Priority,
    Hinted,
    Optimistic,

    // received messages
    InvalidHeader,
    InvalidMessageType,
    InvalidKeepaliveMessage,
    InvalidPublishMessage,
    InvalidConfirmReqMessage,
    InvalidConfirmAckMessage,
    InvalidNodeIdHandshakeMessage,
    InvalidTelemetryReqMessage,
    InvalidTelemetryAckMessage,
    InvalidBulkPullMessage,
    InvalidBulkPullAccountMessage,
    InvalidFrontierReqMessage,
    InvalidAscPullReqMessage,
    InvalidAscPullAckMessage,
    MessageSizeTooBig,
    OutdatedVersion,

    // network
    LoopKeepalive,
    LoopReachoutCached,
    ReachoutLive,
    ReachoutCached,

    // traffic
    Generic,
    BootstrapServer,
    BootstrapRequests,
    BlockBroadcast,
    BlockBroadcastRpc,
    BlockBroadcastInitial,
    ConfirmationRequests,
    VoteRebroadcast,
    RepCrawler,
    VoteReply,
    Telemetry,

    // tcp_channels
    ChannelAccepted,
    Outdated,

    // tcp_server
    HandshakeAbort,
    HandshakeError,

    // confirmation height
    BlocksConfirmed,

    // request aggregator
    AggregatorAccepted,
    AggregatorDropped,

    // requests
    RequestsCachedHashes,
    RequestsGeneratedHashes,
    RequestsCachedVotes,
    RequestsGeneratedVotes,
    RequestsCannotVote,
    RequestsUnknown,
    RequestsNonFinal,
    RequestsFinal,

    // request_aggregator
    RequestHashes,
    OverfillHashes,
    NormalVote,
    FinalVote,

    // duplicate
    DuplicatePublishMessage,
    DuplicateConfirmAckMessage,

    // telemetry
    InvalidSignature,
    NodeIdMismatch,
    GenesisMismatch,
    EmptyPayload,
    CleanupOutdated,

    // vote generator
    GeneratorBroadcasts,
    GeneratorReplies,
    GeneratorRepliesDiscarded,
    GeneratorSpacing,
    SentPr,
    SentNonPr,

    // hinting
    MissingBlock,
    DependentUnconfirmed,
    AlreadyConfirmed,
    Activate,
    ActivateImmediate,

    // bootstrap server
    Response,
    Blocks,
    ChannelFull,
    Frontiers,
    AccountInfo,

    // backlog
    Activated,
    ActivateFailed,
    ActivateSkip,
    ActivateFull,
    Scanned,

    // active
    Insert,
    InsertFailed,
    ActivateImmediately,

    // election scheduler
    InsertManual,
    EraseOldest,

    // bootstrap
    MissingTag,
    Reply,
    Timeout,
    NothingNew,
    AccountInfoEmpty,
    LoopDependencies,
    LoopFrontiers,
    InvalidResponseType,
    InvalidResponse,
    TimestampReset,

    Prioritize,
    PrioritizeFailed,
    Block,
    BlockFailed,
    Unblock,
    UnblockFailed,
    DependencyUpdate,
    DependencyUpdateFailed,

    NextNone,
    NextBlocking,
    NextFrontier,

    PriorityInsert,
    PriorityErase,
    PriorityUnblocked,
    PriorityEraseThreshold,
    PriorityEraseBlock,
    Deprioritize,
    DeprioritizeFailed,
    SyncDependencies,
    BlockingDecayed,
    DependencySynced,

    // rep_crawler
    QuerySent,
    QueryDuplicate,
    QueryTimeout,
    QueryCompletion,
    CrawlAggressive,
    CrawlNormal,

    // rep tiers
    Tier1,
    Tier2,
    Tier3,

    // confirming_set
    NotifyIntermediate,
    AlreadyCemented,
    Cementing,
    CementedHash,
    CementingFailed,

    // election_state
    Passive,
    Active,
    ExpiredConfirmed,
    ExpiredUnconfirmed,
    Cancelled,

    // election_status_type
    ActiveConfirmedQuorum,
    ActiveConfirmationHeight,
    InactiveConfirmationHeight,

    // query_type
    BlocksByHash,
    BlocksByAccount,
    AccountInfoByHash,

    // bounded backlog
    GatheredTargets,
    PerformingRollbacks,
    NoTargets,
    RollbackMissingBlock,
    RollbackSkipped,
    LoopScan,

    // pruning
    LedgerPruning,
    PruningTarget,
    PrunedCount,
    CollectTargets,
}

impl DetailType {
    pub fn as_str(&self) -> &'static str {
        to_variant_name(self).unwrap_or_default()
    }
}

/// Direction of the stat. If the direction is irrelevant, use In
#[derive(FromPrimitive, PartialEq, PartialOrd, Eq, Ord, Clone, Copy, Debug, Hash)]
#[repr(u8)]
pub enum Direction {
    In,
    Out,
}

impl Direction {
    pub fn as_str(&self) -> &'static str {
        match self {
            Direction::In => "in",
            Direction::Out => "out",
        }
    }
}

#[repr(u8)]
#[derive(FromPrimitive, Serialize, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Debug)]
#[serde(rename_all = "snake_case")]
pub enum Sample {
    ActiveElectionDuration,
    BootstrapTagDuration,
    RepResponseTime,
    VoteGeneratorFinalHashes,
    VoteGeneratorHashes,
}

impl Sample {
    pub fn as_str(&self) -> &'static str {
        to_variant_name(self).unwrap_or_default()
    }
}
