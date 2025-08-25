use rsnano_rpc_messages::{DeterministicKeyArgs, KeyPairDto};
use rsnano_types::{Account, PublicKey};

pub fn deterministic_key(args: DeterministicKeyArgs) -> KeyPairDto {
    let private = rsnano_types::deterministic_key(&args.seed, args.index.inner());
    let public: PublicKey = (&private).try_into().unwrap();
    let account = Account::from(public);
    KeyPairDto::new(private, public, account)
}
