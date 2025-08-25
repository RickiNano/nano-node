use crate::config::NodeConfig;
use rsnano_types::utils::Peer;
use serde::{Deserialize, Serialize};
use std::{str::FromStr, time::Duration};

#[derive(Deserialize, Serialize)]
pub struct ExperimentalToml {
    pub max_pruning_age: Option<u64>,
    pub max_pruning_depth: Option<u64>,
    pub secondary_work_peers: Option<Vec<String>>,
}

impl NodeConfig {
    pub fn merge_experimental_toml(&mut self, toml: &ExperimentalToml) {
        if let Some(max) = toml.max_pruning_age {
            self.max_pruning_age = Duration::from_secs(max);
        }
        if let Some(max) = toml.max_pruning_depth {
            self.max_pruning_depth = max;
        }
        if let Some(peers) = &toml.secondary_work_peers {
            self.secondary_work_peers = peers
                .iter()
                .map(|string| Peer::from_str(&string).expect("Invalid secondary work peer"))
                .collect();
        }
    }
}

impl From<&NodeConfig> for ExperimentalToml {
    fn from(config: &NodeConfig) -> Self {
        Self {
            secondary_work_peers: Some(
                config
                    .secondary_work_peers
                    .iter()
                    .map(|peer| peer.to_string())
                    .collect(),
            ),
            max_pruning_age: Some(config.max_pruning_age.as_secs()),
            max_pruning_depth: Some(config.max_pruning_depth),
        }
    }
}
