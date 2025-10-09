use crate::{
    domain::{AccountMap, BlockFactory, BlockResult, DelayedBlocks, Forks, RateSpec, SpamStrategy},
    high_prio_check::HighPrioTracker,
};
use rsnano_network::token_bucket::TokenBucket;
use rsnano_nullable_clock::Timestamp;
use rsnano_types::BlockHash;
use std::time::Duration;

pub(crate) struct SpamSpec {
    pub(crate) spam_strategy: SpamStrategy,
    pub(crate) max_blocks: usize,
    pub(crate) rate: RateSpec,
    pub(crate) fork_probability: f64,
    pub(crate) track_confirmations: bool,
}

pub(crate) struct SpamLogic {
    pub(crate) delayed: DelayedBlocks,
    pub(crate) high_prio_tracker: HighPrioTracker,
    pub(crate) block_factory: BlockFactory,
    pub(crate) current_bps: usize,
    limiter: TokenBucket,
    next_block: Option<Forks>,
    bps_start: Option<Timestamp>,
    spec: SpamSpec,
    pub(crate) total: usize,
    pub(crate) confirmed: usize,
    pub(crate) sum_conf_time: Duration,
    pub(crate) sum_conf_time_total: Duration,
}

impl SpamLogic {
    pub(crate) fn new(account_map: AccountMap, spec: SpamSpec) -> Self {
        Self {
            delayed: Default::default(),
            high_prio_tracker: Default::default(),
            block_factory: BlockFactory::new(account_map, spec.max_blocks, spec.spam_strategy),
            current_bps: spec.rate.initial_bps,
            limiter: TokenBucket::new(spec.rate.initial_bps),
            next_block: None,
            bps_start: None,
            spec,
            total: 0,
            confirmed: 0,
            sum_conf_time: Duration::ZERO,
            sum_conf_time_total: Duration::ZERO,
        }
    }

    pub(crate) fn fork_propability(&self) -> f64 {
        self.spec.fork_probability
    }

    pub(crate) fn next_block(&mut self, is_fork: bool, now: Timestamp) -> Option<BlockResult> {
        if self.block_factory.max_blocks_reached() {
            self.delayed.finished();
            return None;
        }

        if self.bps_start.is_none() {
            self.bps_start = Some(now);
        }

        if self.next_block.is_none() {
            match self.block_factory.create_next(is_fork) {
                Some(BlockResult::Block(b)) => {
                    self.next_block = Some(b);
                }
                Some(BlockResult::Waiting) => return Some(BlockResult::Waiting),
                None => unreachable!(),
            }
        }

        if !self.limiter.try_consume(1, now) {
            return Some(BlockResult::Waiting);
        }

        let next = self.next_block.take().unwrap();
        self.delayed.insert(next.block.clone()); // TODO: handle forks!

        if self.bps_start.unwrap().elapsed(now) >= self.spec.rate.interval {
            self.current_bps += self.spec.rate.increment;
            self.limiter.set_limit(self.current_bps);
            self.bps_start = Some(now);
        }

        Some(BlockResult::Block(next))
    }

    pub(crate) fn confirmed(&mut self, block_hash: &BlockHash, timestamp: Timestamp) {
        if self.spec.track_confirmations {
            let conf_time = self.delayed.confirmed(block_hash, timestamp);

            if let Some(conf_time) = conf_time {
                self.confirmed += 1;
                self.total += 1;
                self.sum_conf_time += conf_time;
                self.sum_conf_time_total += conf_time;
            }
            self.block_factory.confirm(block_hash);
        }

        self.high_prio_tracker.confirmed(block_hash);
    }

    pub(crate) fn reset_cps_counter(&mut self) {
        self.confirmed = 0;
        self.sum_conf_time = Duration::ZERO;
    }
}
