use rsnano_rpc_messages::{AccountCandidateArg, ValidResponse};
use rsnano_types::Account;

pub fn validate_account_number(args: AccountCandidateArg) -> ValidResponse {
    let valid = Account::parse(&args.account).is_some();
    ValidResponse::new(valid)
}
