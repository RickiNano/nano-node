use rsnano_rpc_messages::{DeterministicKeyArgs, KeyPairDto};

pub fn deterministic_key(args: DeterministicKeyArgs) -> KeyPairDto {
    let private_key = rsnano_types::deterministic_key(&args.seed, args.index.inner());
    KeyPairDto::new(private_key)
}
