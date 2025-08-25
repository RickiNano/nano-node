use rsnano_rpc_messages::{AccountCandidateArg, ValidResponse};
use rsnano_types::Account;

pub fn validate_account_number(args: AccountCandidateArg) -> ValidResponse {
    let valid = Account::decode_account(&args.account).is_ok();
    ValidResponse::new(valid)
}
