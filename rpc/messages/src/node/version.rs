use crate::{RpcU8, RpcU32};
use rsnano_types::BlockHash;
use serde::{Deserialize, Serialize};

#[derive(PartialEq, Eq, Debug, Serialize, Deserialize)]
pub struct VersionResponse {
    pub rpc_version: RpcU8,
    pub store_version: RpcU32,
    pub protocol_version: RpcU8,
    pub node_vendor: String,
    pub store_vendor: String,
    pub network: String,
    pub network_identifier: BlockHash,
    pub build_info: String,
}
