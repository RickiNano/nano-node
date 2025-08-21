use rsnano_core::{Account, Amount, Networks, PublicKey};
use rsnano_ledger::DEV_GENESIS_PUB_KEY;
use std::time::Duration;

#[derive(Clone)]
pub struct WalletsConfig {
    pub preconfigured_representatives: Vec<PublicKey>,
    pub password_fanout: usize,
    pub receive_minimum: Amount,
    pub vote_minimum: Amount,
    pub voting_enabled: bool,
    /// How long to wait until the next cached work is created
    pub cached_work_generation_delay: Duration,
    pub kdf_work: u32,
}

impl WalletsConfig {
    pub fn default_for(network: Networks) -> Self {
        match network {
            Networks::Invalid => unreachable!(),
            Networks::NanoDevNetwork => Self::defaults_dev(),
            Networks::NanoBetaNetwork => Self::defaults_beta(),
            Networks::NanoLiveNetwork => Self::defaults_live(),
            Networks::NanoTestNetwork => Self::defaults_test(),
        }
    }

    pub fn defaults_live() -> Self {
        Self {
            preconfigured_representatives: default_preconfigured_representatives_for_live(),
            password_fanout: 1024,
            receive_minimum: Amount::micronano(1),
            vote_minimum: Amount::nano(1000),
            voting_enabled: false,
            cached_work_generation_delay: Duration::from_secs(10),
            kdf_work: 1024 * 64,
        }
    }

    pub fn defaults_dev() -> Self {
        Self {
            voting_enabled: true,
            preconfigured_representatives: vec![*DEV_GENESIS_PUB_KEY],
            cached_work_generation_delay: Duration::from_secs(1),
            kdf_work: 8,
            ..Self::defaults_live()
        }
    }

    pub fn defaults_beta() -> Self {
        Self {
            preconfigured_representatives: vec![Account::decode_account(
                "nano_1defau1t9off1ine9rep99999999999999999999999999999999wgmuzxxy",
            )
            .unwrap()
            .into()],
            ..Self::defaults_live()
        }
    }

    pub fn defaults_test() -> Self {
        Self {
            preconfigured_representatives: Vec::new(),
            ..Self::defaults_live()
        }
    }
}

impl Default for WalletsConfig {
    fn default() -> Self {
        Self::defaults_live()
    }
}

pub fn default_preconfigured_representatives_for_live() -> Vec<PublicKey> {
    const REP_KEYS: [&'static str; 8] = [
        "A30E0A32ED41C8607AA9212843392E853FCBCB4E7CB194E35C94F07F91DE59EF",
        "67556D31DDFC2A440BF6147501449B4CB9572278D034EE686A6BEE29851681DF",
        "5C2FBB148E006A8E8BA7A75DD86C9FE00C83F5FFDBFD76EAA09531071436B6AF",
        "AE7AC63990DAAAF2A69BF11C913B928844BF5012355456F2F164166464024B29",
        "BD6267D6ECD8038327D2BCC0850BDF8F56EC0414912207E81BCF90DFAC8A4AAA",
        "2399A083C600AA0572F5E36247D978FCFC840405F8D4B6D33161C0066A55F431",
        "2298FAB7C61058E77EA554CB93EDEEDA0692CBFCC540AB213B2836B29029E23A",
        "3FE80B4BC842E82C1C18ABFEEC47EA989E63953BC82AC411F304D13833D52A56",
    ];

    REP_KEYS
        .iter()
        .map(|s| PublicKey::decode_hex(s).unwrap())
        .collect()
}
