use anyhow::anyhow;
use rsnano_rpc_messages::{KeyExpandArgs, KeyPairDto};
use rsnano_types::{Account, PublicKey};

pub fn key_expand(args: KeyExpandArgs) -> anyhow::Result<KeyPairDto> {
    let public: PublicKey = (&args.key)
        .try_into()
        .map_err(|_| anyhow!("Bad private key"))?;
    let account = Account::from(public);
    Ok(KeyPairDto::new(args.key, public, account))
}
