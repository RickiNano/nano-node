use std::{sync::Arc, time::Duration};
use tracing::trace;

use rsnano_messages::{AscPullAck, AscPullAckType, AscPullReqType, BlocksAckPayload};
use rsnano_network::{Channel, ChannelId};
use rsnano_nullable_clock::Timestamp;
use rsnano_types::{Account, BlockHash};
use rsnano_utils::{
    container_info::{ContainerInfo, ContainerInfoProvider},
    stats::{StatsCollection, StatsSource},
};

use super::{
    CandidateAccounts, PeerScoring, PriorityResult, RunningQueryContainer,
    running_query::QuerySource,
};
use crate::bootstrap::{
    AscPullQuerySpec, BootstrapConfig,
    state::{
        PriorityDownResult, QueryType, RunningQuery, VerifyResult,
        account_ack_processor::AccountAckProcessor,
        block_queue::{AccountBlocks, BlockQueue},
        frontiers_processor::{FrontiersProcessor, OutdatedAccounts},
    },
};

pub struct BootstrapLogic {
    pub candidate_accounts: CandidateAccounts,
    pub(crate) scoring: PeerScoring,
    pub(crate) running_queries: RunningQueryContainer,
    pub(crate) block_queue: BlockQueue,
    pub(crate) block_ack_stats: BlockAckStats,
    pub(crate) stopped: bool,
    account_ack_processor: AccountAckProcessor,
    pub frontiers_processor: FrontiersProcessor,
}

impl BootstrapLogic {
    pub fn new(config: BootstrapConfig) -> Self {
        let mut scoring = PeerScoring::new();
        scoring.set_channel_limit(config.channel_limit);

        Self {
            candidate_accounts: CandidateAccounts::new(config.candidate_accounts.clone()),
            scoring,
            running_queries: RunningQueryContainer::default(),
            block_queue: BlockQueue::default(),
            block_ack_stats: Default::default(),
            stopped: false,
            account_ack_processor: Default::default(),
            frontiers_processor: FrontiersProcessor::new(config.frontier_scan.clone()),
        }
    }

    pub fn next_blocking_query(&self, channel: &Arc<Channel>) -> Option<AscPullQuerySpec> {
        let next = self.next_blocking();
        if !next.is_zero() {
            Some(Self::create_blocking_query(next, channel.clone()))
        } else {
            None
        }
    }

    fn create_blocking_query(next: BlockHash, channel: Arc<Channel>) -> AscPullQuerySpec {
        AscPullQuerySpec {
            query_id: 0, // TODO
            channel,
            req_type: AscPullReqType::account_info_by_hash(next),
            account: Account::ZERO,
            hash: next,
            cooldown_account: false,
        }
    }

    fn count_queries_by_hash(&self, hash: &BlockHash, source: QuerySource) -> usize {
        self.running_queries
            .iter_hash(hash)
            .filter(|i| i.source == source)
            .count()
    }

    pub fn next_priority(&mut self, now: Timestamp) -> PriorityResult {
        let next = self.candidate_accounts.next_priority(now, |account| {
            !self.block_queue.contains_account(account)
                && self
                    .running_queries
                    .count_by_account(account, QuerySource::Priority)
                    < 4
        });

        if next.account.is_zero() {
            return Default::default();
        }

        next
    }

    /* Waits for next available blocking block */
    pub fn next_blocking(&self) -> BlockHash {
        self.candidate_accounts
            .next_blocking(|hash| self.count_queries_by_hash(hash, QuerySource::Dependencies) == 0)
    }

    fn take_running_query_for(
        &mut self,
        response: &AscPullAck,
    ) -> Result<RunningQuery, ProcessError> {
        // Only process messages that have a known running query
        let Some(query) = self.running_queries.remove(response.id) else {
            return Err(ProcessError::NoRunningQueryFound);
        };

        if !query.is_valid_response_type(response) {
            return Err(ProcessError::InvalidResponseType);
        }

        Ok(query)
    }

    pub(crate) fn process_response(
        &mut self,
        response: AscPullAck,
        channel_id: ChannelId,
        now: Timestamp,
    ) -> Result<ProcessInfo, ProcessError> {
        let query = self.take_running_query_for(&response)?;
        self.scoring.received_message(channel_id);
        self.process_response_for_query(&query, response)
            .map(|_| ProcessInfo::new(&query, now))
    }

    fn process_response_for_query(
        &mut self,
        query: &RunningQuery,
        response: AscPullAck,
    ) -> Result<(), ProcessError> {
        let ok = match response.pull_type {
            AscPullAckType::Blocks(blocks) => self.process_blocks(query, blocks),
            AscPullAckType::AccountInfo(info) => {
                self.account_ack_processor
                    .process(&mut self.candidate_accounts, query, &info)
            }
            AscPullAckType::Frontiers(frontiers) => {
                self.frontiers_processor.process(query, frontiers)
            }
        };

        if ok {
            Ok(())
        } else {
            Err(ProcessError::InvalidResponse)
        }
    }

    pub fn frontiers_processed(&mut self, outdated: &OutdatedAccounts) {
        self.frontiers_processor
            .frontiers_processed(outdated, &mut self.candidate_accounts);
    }

    // block ack handling:
    //********************************************************************************
    fn process_blocks(&mut self, query: &RunningQuery, response: BlocksAckPayload) -> bool {
        trace!(
            query_id = query.id,
            blocks = response.blocks().len(),
            "Process response"
        );

        self.block_ack_stats.process += 1;

        let result = query.verify_blocks(&response);
        match result {
            VerifyResult::Ok => {
                self.process_valid_blocks(query, response);
                true
            }
            VerifyResult::NothingNew => {
                self.process_empty_response(query);
                true
            }
            VerifyResult::Invalid => {
                self.block_ack_stats.invalid += 1;
                false
            }
        }
    }

    fn process_valid_blocks(&mut self, query: &RunningQuery, response: BlocksAckPayload) {
        self.block_ack_stats.verified += 1;
        self.block_ack_stats.blocks += response.blocks().len() as u64;

        let mut blocks = response.take_blocks();

        // Avoid re-processing the block we already have
        assert!(blocks.len() >= 1);
        if blocks.front().unwrap().hash() == query.start.into() {
            blocks.pop_front();
        }

        self.block_queue.insert(AccountBlocks {
            account: query.account,
            query_id: query.id,
            blocks: blocks.clone(),
        });
    }

    fn process_empty_response(&mut self, query: &RunningQuery) {
        self.block_ack_stats.nothing_new += 1;
        {
            match self.candidate_accounts.priority_down(&query.account) {
                PriorityDownResult::Deprioritized => {
                    self.block_ack_stats.deprioritize += 1;
                }
                PriorityDownResult::Erased => {
                    self.block_ack_stats.deprioritize += 1;
                    self.block_ack_stats.priority_erase_theshold += 1;
                }
                PriorityDownResult::AccountNotFound => {
                    self.block_ack_stats.deprioritize_failed += 1;
                }
                PriorityDownResult::InvalidAccount => {}
            }

            self.candidate_accounts.reset_last_request(&query.account);
        }
    }

    //********************************************************************************

    pub fn container_info(&self) -> ContainerInfo {
        ContainerInfo::builder()
            .leaf(
                "tags",
                self.running_queries.len(),
                RunningQueryContainer::ELEMENT_SIZE,
            )
            .node("accounts", self.candidate_accounts.container_info())
            .node("frontiers", self.frontiers_processor.container_info())
            .node("peers", self.scoring.container_info())
            .finish()
    }
}

impl Default for BootstrapLogic {
    fn default() -> Self {
        Self::new(Default::default())
    }
}

impl StatsSource for BootstrapLogic {
    fn collect_stats(&self, result: &mut StatsCollection) {
        self.frontiers_processor.collect_stats(result);
        self.account_ack_processor.collect_stats(result);
        self.block_ack_stats.collect_stats(result);
    }
}

#[derive(Debug)]
pub(crate) enum ProcessError {
    NoRunningQueryFound,
    InvalidResponseType,
    InvalidResponse,
}

pub(crate) struct ProcessInfo {
    pub query_type: QueryType,
    pub response_time: Duration,
}

impl ProcessInfo {
    pub fn new(query: &RunningQuery, now: Timestamp) -> Self {
        Self {
            query_type: query.query_type,
            response_time: query.sent.elapsed(now),
        }
    }
}

#[derive(Default)]
pub(crate) struct BlockAckStats {
    process: u64,
    invalid: u64,
    verified: u64,
    blocks: u64,
    nothing_new: u64,
    deprioritize: u64,
    priority_erase_theshold: u64,
    deprioritize_failed: u64,
}

impl StatsSource for BlockAckStats {
    fn collect_stats(&self, result: &mut StatsCollection) {
        result.insert("bootstrap_process", "blocks", self.process);
        result.insert("bootstrap_verify_blocks", "invalid", self.invalid);
        result.insert("bootstrap_verify_blocks", "ok", self.verified);
        result.insert("bootstrap", "blocks", self.blocks);
        result.insert("bootstrap_verify_blocks", "nothing_new", self.nothing_new);
        result.insert("bootstrap_account_sets", "deprioritize", self.deprioritize);
        result.insert(
            "bootstrap_account_sets",
            "priority_erase_threshold",
            self.priority_erase_theshold,
        );
        result.insert(
            "bootstrap_account_sets",
            "deprioritize_failed",
            self.deprioritize_failed,
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::bootstrap::state::QueryType;

    mod block_ack {
        use super::*;

        #[test]
        fn response_doesnt_match_query() {
            let mut logic = BootstrapLogic::default();

            let query = RunningQuery::new_test_instance();
            let response = BlocksAckPayload::new_test_instance();
            let ok = logic.process_blocks(&query, response);
            assert!(!ok);
            assert_eq!(logic.block_ack_stats.process, 1);
            assert_eq!(logic.block_ack_stats.invalid, 1);
        }

        #[test]
        fn handle_empty_response() {
            let mut logic = BootstrapLogic::default();
            let account = Account::from(42);

            let query = RunningQuery {
                query_type: QueryType::BlocksByAccount,
                account,
                ..RunningQuery::new_test_instance()
            };

            let response = BlocksAckPayload::empty();
            let ok = logic.process_blocks(&query, response);
            assert!(ok);
            assert_eq!(logic.block_ack_stats.process, 1);
            assert_eq!(logic.block_ack_stats.nothing_new, 1);
        }
    }
}
