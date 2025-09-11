use rsnano_messages::AccountInfoAckPayload;
use rsnano_types::{Account, BlockHash};
use rsnano_utils::stats::{StatsCollection, StatsSource};

use crate::bootstrap::state::{CandidateAccounts, RunningQuery};

#[derive(Default)]
pub(super) struct AccountAckProcessor {
    stats: AccountAckStats,
}

impl AccountAckProcessor {
    pub fn process(
        &mut self,
        candidates: &mut CandidateAccounts,
        query: &RunningQuery,
        response: &AccountInfoAckPayload,
    ) -> bool {
        if response.account.is_zero() {
            self.stats.empty += 1;
            // OK, but nothing to do
            return true;
        }

        self.stats.process += 1;

        // Prioritize account containing the dependency
        self.update_dependency(candidates, &query.hash, response.account);
        self.prioritize(candidates, &response.account);

        // OK, no way to verify the response
        true
    }

    fn update_dependency(
        &mut self,
        candidates: &mut CandidateAccounts,
        hash: &BlockHash,
        dep_account: Account,
    ) {
        let updated = candidates.dependency_update(hash, dep_account);

        if updated > 0 {
            self.stats.dependency_update += updated as u64;
        } else {
            self.stats.dependency_update_failed += 1;
        }
    }

    fn prioritize(&mut self, candidates: &mut CandidateAccounts, account: &Account) {
        // Use the lowest possible priority here
        if candidates.priority_set(account, CandidateAccounts::PRIORITY_CUTOFF) {
            self.stats.priority_insert += 1;
        } else {
            self.stats.prioritize_failed += 1;
        };
    }
}

impl StatsSource for AccountAckProcessor {
    fn collect_stats(&self, result: &mut StatsCollection) {
        self.stats.collect_stats(result);
    }
}

#[derive(Default)]
pub(super) struct AccountAckStats {
    pub empty: u64,
    pub process: u64,
    pub dependency_update: u64,
    pub dependency_update_failed: u64,
    pub priority_insert: u64,
    pub prioritize_failed: u64,
}

impl StatsSource for AccountAckStats {
    fn collect_stats(&self, result: &mut StatsCollection) {
        result.insert("bootstrap_process", "account_info", self.process);
        result.insert("bootstrap_process", "account_info_empty", self.empty);
        result.insert(
            "bootstrap_account_sets",
            "dependency_update",
            self.dependency_update,
        );
        result.insert(
            "bootstrap_account_sets",
            "dependency_update_failed",
            self.dependency_update_failed,
        );
        result.insert(
            "bootstrap_account_sets",
            "priority_insert",
            self.priority_insert,
        );
        result.insert(
            "bootstrap_account_sets",
            "prioritize_failed",
            self.prioritize_failed,
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rsnano_nullable_clock::Timestamp;

    #[test]
    fn empty_response() {
        let mut processor = AccountAckProcessor::default();
        let mut candidates = CandidateAccounts::default();
        let query = RunningQuery::new_test_instance();

        let response = AccountInfoAckPayload {
            account: Account::ZERO,
            ..AccountInfoAckPayload::new_test_instance()
        };

        assert!(processor.process(&mut candidates, &query, &response));

        assert_eq!(processor.stats.empty, 1);
        assert_eq!(processor.stats.process, 0);
        assert_eq!(candidates.priority_len(), 0);
    }

    #[test]
    fn when_not_blocked_should_only_prioritize() {
        let mut processor = AccountAckProcessor::default();
        let mut candidates = CandidateAccounts::default();
        let query = RunningQuery::new_test_instance();
        let response = AccountInfoAckPayload::new_test_instance();

        assert!(processor.process(&mut candidates, &query, &response));

        assert_eq!(processor.stats.process, 1);
        assert!(candidates.prioritized(&response.account));
        assert_eq!(processor.stats.dependency_update_failed, 1);
        assert_eq!(processor.stats.priority_insert, 1);
    }

    #[test]
    fn update_dependency() {
        let mut processor = AccountAckProcessor::default();
        let mut candidates = CandidateAccounts::default();
        let blocked_account = Account::from(100);
        let unknown_source = BlockHash::from(42);
        let source_account = Account::from(200);

        let query = RunningQuery {
            hash: unknown_source,
            ..RunningQuery::new_test_instance()
        };

        let response = AccountInfoAckPayload {
            account: source_account,
            ..AccountInfoAckPayload::new_test_instance()
        };

        candidates.priority_set_initial(&blocked_account);

        candidates.block(
            blocked_account,
            unknown_source,
            Timestamp::new_test_instance(),
        );

        assert!(processor.process(&mut candidates, &query, &response));

        assert!(candidates.blocked(&blocked_account));
        assert!(candidates.prioritized(&source_account));
        let query = candidates.next_priority(Timestamp::new_test_instance(), |_| true);
        assert_eq!(query.account, source_account);
        assert_eq!(processor.stats.dependency_update, 1);
        assert_eq!(processor.stats.priority_insert, 1);
    }

    #[test]
    fn dependency_update_fails() {
        let mut processor = AccountAckProcessor::default();
        let mut candidates = CandidateAccounts::default();

        let blocked_account = Account::from(100);
        let unknown_source = BlockHash::from(42);
        let source_account = Account::from(200);

        let query = RunningQuery {
            hash: unknown_source,
            ..RunningQuery::new_test_instance()
        };

        let response = AccountInfoAckPayload {
            account: source_account,
            ..AccountInfoAckPayload::new_test_instance()
        };

        candidates.priority_set_initial(&blocked_account);
        candidates.block(
            blocked_account,
            unknown_source,
            Timestamp::new_test_instance(),
        );
        candidates.dependency_update(&unknown_source, source_account);
        candidates.priority_set_initial(&source_account);

        assert!(processor.process(&mut candidates, &query, &response));

        assert_eq!(processor.stats.dependency_update_failed, 1);
        assert_eq!(processor.stats.prioritize_failed, 1);
    }
}
