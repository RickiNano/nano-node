use std::{
    sync::{Arc, Mutex},
    time::Duration,
};
use tracing::trace;

use rsnano_ledger::Ledger;
use rsnano_messages::{AscPullAck, AscPullAckType};
use rsnano_network::ChannelId;
use rsnano_nullable_clock::Timestamp;
use rsnano_utils::stats::Stats;

use super::super::state::{BootstrapLogic, QueryType, RunningQuery};
use crate::{
    block_processing::{BlockContext, BlockProcessorQueue, BlockSource},
    bootstrap::response_processor::frontier_check_pool::FrontierCheckPool,
};

pub(crate) struct ResponseProcessor {
    logic: Arc<Mutex<BootstrapLogic>>,
    frontier_check_pool: FrontierCheckPool,
    block_queue: Arc<BlockProcessorQueue>,
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

impl ResponseProcessor {
    pub(crate) fn new(
        logic: Arc<Mutex<BootstrapLogic>>,
        stats: Arc<Stats>,
        block_queue: Arc<BlockProcessorQueue>,
        ledger: Arc<Ledger>,
    ) -> Self {
        let frontier_check_pool = FrontierCheckPool::new(stats.clone(), ledger, logic.clone());

        Self {
            logic,
            frontier_check_pool,
            block_queue,
        }
    }

    pub fn set_max_pending_frontiers(&mut self, max_pending: usize) {
        self.frontier_check_pool.max_pending = max_pending;
    }

    pub fn process(
        &self,
        response: AscPullAck,
        channel_id: ChannelId,
        now: Timestamp,
    ) -> Result<ProcessInfo, ProcessError> {
        trace!(query_id = response.id, ?channel_id, "Process response");
        let query = self.take_running_query_for(&response)?;
        self.process_response(&query, response)?;
        self.update_peer_scoring(channel_id);
        Ok(ProcessInfo::new(&query, now))
    }

    fn take_running_query_for(&self, response: &AscPullAck) -> Result<RunningQuery, ProcessError> {
        let mut guard = self.logic.lock().unwrap();

        // Only process messages that have a known running query
        let Some(query) = guard.running_queries.remove(response.id) else {
            return Err(ProcessError::NoRunningQueryFound);
        };

        if !query.is_valid_response_type(response) {
            return Err(ProcessError::InvalidResponseType);
        }

        Ok(query)
    }

    fn update_peer_scoring(&self, channel_id: ChannelId) {
        self.logic
            .lock()
            .unwrap()
            .scoring
            .received_message(channel_id);
    }

    fn process_response(
        &self,
        query: &RunningQuery,
        response: AscPullAck,
    ) -> Result<(), ProcessError> {
        let mut logic = self.logic.lock().unwrap();
        let ok = match response.pull_type {
            AscPullAckType::Blocks(blocks) => logic.process_blocks(query, blocks),
            AscPullAckType::AccountInfo(info) => logic.process_account_ack(query, &info),
            AscPullAckType::Frontiers(frontiers) => logic.process_frontiers(query, frontiers),
        };

        self.enqueue_next_blocks(&mut logic);

        if ok {
            self.frontier_check_pool.enqueue_frontiers(&mut logic);
            Ok(())
        } else {
            Err(ProcessError::InvalidResponse)
        }
    }

    // TODO Remeove duplication! Copied from BlockInspector
    fn enqueue_next_blocks(&self, logic: &mut BootstrapLogic) {
        while let Some((block, query_id)) = logic.block_queue.next_to_process() {
            let block_hash = block.hash();

            trace!(%block_hash, query_id, "Process block");

            let inserted = self.block_queue.push(BlockContext::new(
                block.clone(),
                BlockSource::Bootstrap,
                // TODO use real channel id
                ChannelId::LOOPBACK,
            ));

            if inserted {
                logic.block_queue.enqueued_for_processing(&block_hash);
            } else {
                // block processor queue is full!
                break;
            }
        }
    }
}
