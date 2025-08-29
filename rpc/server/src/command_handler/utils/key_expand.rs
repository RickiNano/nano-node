use rsnano_rpc_messages::{KeyExpandArgs, KeyPairDto};

pub fn key_expand(args: KeyExpandArgs) -> anyhow::Result<KeyPairDto> {
    Ok(KeyPairDto::new(args.key))
}
