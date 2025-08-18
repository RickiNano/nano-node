use super::{GlobalConfig, NodeConfig};
use crate::{
    block_processing::{BacklogScanConfig, ProcessQueueConfig},
    wallets::WalletsConfig,
};

impl From<&GlobalConfig> for ProcessQueueConfig {
    fn from(value: &GlobalConfig) -> Self {
        value.node_config.block_processor.clone()
    }
}

impl From<&GlobalConfig> for BacklogScanConfig {
    fn from(value: &GlobalConfig) -> Self {
        value.node_config.backlog_scan.clone()
    }
}

impl From<&NodeConfig> for WalletsConfig {
    fn from(value: &NodeConfig) -> Self {
        Self {
            preconfigured_representatives: value.preconfigured_representatives.clone(),
            password_fanout: value.password_fanout as usize,
            receive_minimum: value.receive_minimum,
            vote_minimum: value.vote_minimum,
            enable_voting: value.enable_voting,
        }
    }
}
