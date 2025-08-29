use rsnano_rpc_messages::KeyPairDto;
use rsnano_types::PrivateKey;

pub(crate) fn key_create() -> KeyPairDto {
    KeyPairDto::new(PrivateKey::new().raw_key())
}
