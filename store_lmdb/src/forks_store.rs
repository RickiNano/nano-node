use rsnano_nullable_lmdb::{
    DatabaseFlags, Error, LmdbDatabase, LmdbEnvironment, Transaction, WriteFlags, WriteTransaction,
};
use rsnano_types::{QualifiedRoot, SnapshotNumber};

/// Maps the qualified roots to the snapshot number when the fork was detected
pub struct LmdbForksStore {
    database: LmdbDatabase,
}

impl LmdbForksStore {
    pub fn new(env: &LmdbEnvironment) -> anyhow::Result<Self> {
        let database = env.create_db(Some("forks"), DatabaseFlags::empty())?;

        Ok(Self { database })
    }

    pub fn database(&self) -> LmdbDatabase {
        self.database
    }

    /// Returns *true* if root was inserted or the same root was already in the database
    pub fn put(
        &self,
        txn: &mut WriteTransaction,
        root: &QualifiedRoot,
        snapshot_number: SnapshotNumber,
    ) {
        txn.put(
            self.database,
            &root.to_bytes(),
            &snapshot_number.to_be_bytes(),
            WriteFlags::empty(),
        )
        .expect("This should never fail");
    }

    pub fn contains(&self, tx: &dyn Transaction, root: &QualifiedRoot) -> bool {
        let result = tx.get(self.database, &root.to_bytes());
        match result {
            Err(Error::NotFound) => false,
            Ok(_) => true,
            Err(e) => panic!("Could not load fork info {:?}", e),
        }
    }

    pub fn del(&self, tx: &mut WriteTransaction, root: &QualifiedRoot) {
        let root_bytes = root.to_bytes();
        tx.delete(self.database, &root_bytes, None).unwrap();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rsnano_nullable_lmdb::{DeleteEvent, PutEvent};
    use std::sync::Arc;

    const TEST_DATABASE: LmdbDatabase = LmdbDatabase::new_null(100);

    struct Fixture {
        env: Arc<LmdbEnvironment>,
        store: LmdbForksStore,
    }

    impl Fixture {
        fn new() -> Self {
            Self::with_stored_entries(Vec::new())
        }

        fn with_stored_entries(entries: Vec<QualifiedRoot>) -> Self {
            let mut env = LmdbEnvironment::null_builder().database("forks", TEST_DATABASE);
            for entry in entries {
                env = env.entry(&entry.to_bytes(), &[]);
            }
            Self::with_env(env.build().build())
        }

        fn with_env(env: LmdbEnvironment) -> Self {
            let env = Arc::new(env);
            Self {
                store: LmdbForksStore::new(&env).unwrap(),
                env,
            }
        }
    }

    #[test]
    fn put() {
        let root = QualifiedRoot::new_test_instance();
        let snapshot_number = 0;
        let fixture = Fixture::new();

        let mut txn = fixture.env.begin_write();
        let put_tracker = txn.track_puts();
        fixture.store.put(&mut txn, &root, snapshot_number);

        let output = put_tracker.output();

        assert_eq!(
            output[0],
            PutEvent {
                database: TEST_DATABASE,
                key: root.to_bytes().to_vec(),
                value: snapshot_number.to_be_bytes().to_vec(),
                flags: WriteFlags::empty()
            }
        )
    }

    #[test]
    fn exists() {
        let root = QualifiedRoot::new_test_instance();
        let fixture = Fixture::with_stored_entries(vec![root.clone()]);
        let txn = fixture.env.begin_read();

        let result = fixture.store.contains(&txn, &root);

        assert_eq!(result, true);
    }

    #[test]
    fn delete() {
        let root = QualifiedRoot::new_test_instance();
        let fixture = Fixture::with_stored_entries(vec![root.clone()]);
        let mut txn = fixture.env.begin_write();
        let delete_tracker = txn.track_deletions();

        fixture.store.del(&mut txn, &root);

        assert_eq!(
            delete_tracker.output(),
            vec![DeleteEvent {
                key: root.to_bytes().to_vec(),
                database: TEST_DATABASE.into(),
            }]
        )
    }
}
