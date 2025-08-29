use std::{fmt::Display, mem::size_of};

use num_traits::FromPrimitive;
use serde::ser::SerializeStruct;
use serde_derive::Serialize;

use rsnano_types::{Account, Amount, DeserializationError, read_u8};

use super::MessageVariant;

#[derive(Clone, Copy, PartialEq, Eq, FromPrimitive, Debug, Serialize)]
#[serde(rename_all = "snake_case")]
#[repr(u8)]
pub enum BulkPullAccountFlags {
    PendingHashAndAmount = 0x0,
    PendingAddressOnly = 0x1,
    PendingHashAmountAndAddress = 0x2,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct BulkPullAccount {
    pub account: Account,
    pub minimum_amount: Amount,
    pub flags: BulkPullAccountFlags,
}

impl BulkPullAccount {
    pub const SERIALIZED_SIZE: usize =
        Account::SERIALIZED_SIZE + Amount::SERIALIZED_SIZE + size_of::<BulkPullAccountFlags>();

    pub fn new_test_instance() -> BulkPullAccount {
        Self {
            account: 1.into(),
            minimum_amount: 42.into(),
            flags: BulkPullAccountFlags::PendingHashAndAmount,
        }
    }

    pub fn serialize<T>(&self, writer: &mut T) -> std::io::Result<()>
    where
        T: std::io::Write,
    {
        self.account.serialize(writer)?;
        self.minimum_amount.serialize(writer)?;
        writer.write_all(&[self.flags as u8])
    }

    pub fn deserialize(mut bytes: &[u8]) -> Result<Self, DeserializationError> {
        let account = Account::deserialize(&mut bytes)?;
        let minimum_amount = Amount::deserialize(&mut bytes)?;
        let flags = BulkPullAccountFlags::from_u8(read_u8(&mut bytes)?)
            .ok_or(DeserializationError::InvalidData)?;

        Ok(Self {
            account,
            minimum_amount,
            flags,
        })
    }
}

impl MessageVariant for BulkPullAccount {}

impl Display for BulkPullAccount {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "\nacc={} min={}",
            self.account.encode_hex(),
            self.minimum_amount.to_string_dec()
        )?;

        let flag_str = match self.flags {
            BulkPullAccountFlags::PendingHashAndAmount => "pending_hash_and_amount",
            BulkPullAccountFlags::PendingAddressOnly => "pending_address_only",
            BulkPullAccountFlags::PendingHashAmountAndAddress => "pending_hash_amount_and_address",
        };

        write!(f, " {}", flag_str)
    }
}

impl serde::Serialize for BulkPullAccount {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let mut state = serializer.serialize_struct("Message", 1)?;
        state.serialize_field("message_type", "bulk_push")?;
        state.end()
    }
}
