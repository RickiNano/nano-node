use std::time::{Duration, Instant};

use super::{ConfiguredDatabase, LmdbDatabase, RoCursor};
use crate::{Transaction, EMPTY_DATABASE};

pub struct ReadTransaction {
    txn: RoTransaction,
    start: Instant,
}

impl ReadTransaction {
    pub fn new(txn: lmdb::RoTransaction<'static>) -> Self {
        Self {
            txn: RoTransaction::new(txn),
            start: Instant::now(),
        }
    }

    pub fn new_null(configured_databases: Vec<ConfiguredDatabase>) -> Self {
        Self {
            txn: RoTransaction::new_null(configured_databases),
            start: Instant::now(),
        }
    }

    pub fn commit(self) {
        self.txn.commit().expect("Commit failed");
    }

    pub fn reset(self) -> InactiveTransaction {
        self.txn.reset()
    }

    pub fn refresh(self) -> Self {
        self.txn.reset().renew().unwrap()
    }
}

impl Transaction for ReadTransaction {
    fn is_refresh_needed(&self) -> bool {
        self.is_refresh_needed_with(Duration::from_millis(500))
    }

    fn is_refresh_needed_with(&self, max_duration: Duration) -> bool {
        self.start.elapsed() > max_duration
    }

    fn get(&self, database: LmdbDatabase, key: &[u8]) -> lmdb::Result<&[u8]> {
        self.txn.get(database, key)
    }

    fn open_ro_cursor(&self, database: LmdbDatabase) -> lmdb::Result<RoCursor> {
        self.txn.open_ro_cursor(database)
    }

    fn count(&self, database: LmdbDatabase) -> u64 {
        self.txn.count(database)
    }
}

struct RoTransaction {
    strategy: RoTransactionStrategy,
}

impl RoTransaction {
    pub fn new(tx: lmdb::RoTransaction<'static>) -> Self {
        Self {
            strategy: RoTransactionStrategy::Real(RoTransactionWrapper(tx)),
        }
    }

    pub fn new_null(databases: Vec<ConfiguredDatabase>) -> Self {
        Self {
            strategy: RoTransactionStrategy::Nulled(RoTransactionStub { databases }),
        }
    }

    pub fn commit(self) -> lmdb::Result<()> {
        if let RoTransactionStrategy::Real(s) = self.strategy {
            s.commit()?;
        }
        Ok(())
    }

    pub fn get(&self, database: LmdbDatabase, key: &[u8]) -> lmdb::Result<&[u8]> {
        match &self.strategy {
            RoTransactionStrategy::Real(s) => s.get(database, key),
            RoTransactionStrategy::Nulled(s) => s.get(database, key),
        }
    }

    pub fn open_ro_cursor(&self, database: LmdbDatabase) -> lmdb::Result<RoCursor> {
        match &self.strategy {
            RoTransactionStrategy::Real(s) => s.open_ro_cursor(database),
            RoTransactionStrategy::Nulled(s) => s.open_ro_cursor(database),
        }
    }

    pub fn count(&self, database: LmdbDatabase) -> u64 {
        match &self.strategy {
            RoTransactionStrategy::Real(s) => s.count(database),
            RoTransactionStrategy::Nulled(s) => s.count(database),
        }
    }

    pub fn reset(self) -> InactiveTransaction {
        match self.strategy {
            RoTransactionStrategy::Real(s) => InactiveTransaction {
                strategy: InactiveTransactionStrategy::Real(s.reset()),
            },
            RoTransactionStrategy::Nulled(s) => InactiveTransaction {
                strategy: InactiveTransactionStrategy::Nulled(s.reset()),
            },
        }
    }
}

enum RoTransactionStrategy {
    Real(RoTransactionWrapper),
    Nulled(RoTransactionStub),
}

struct RoTransactionWrapper(lmdb::RoTransaction<'static>);

impl RoTransactionWrapper {
    fn reset(self) -> InactiveTransactionWrapper {
        InactiveTransactionWrapper {
            inactive: self.0.reset(),
        }
    }

    fn commit(self) -> lmdb::Result<()> {
        lmdb::Transaction::commit(self.0)
    }

    fn get(&self, database: LmdbDatabase, key: &[u8]) -> lmdb::Result<&[u8]> {
        lmdb::Transaction::get(&self.0, database.as_real(), &key)
    }

    fn open_ro_cursor(&self, database: LmdbDatabase) -> lmdb::Result<RoCursor> {
        lmdb::Transaction::open_ro_cursor(&self.0, database.as_real()).map(|c| {
            //todo don't use static lifetime
            let c =
                unsafe { std::mem::transmute::<lmdb::RoCursor<'_>, lmdb::RoCursor<'static>>(c) };
            RoCursor::new(c)
        })
    }

    fn count(&self, database: LmdbDatabase) -> u64 {
        let stat = lmdb::Transaction::stat(&self.0, database.as_real());
        stat.unwrap().entries() as u64
    }
}

struct RoTransactionStub {
    databases: Vec<ConfiguredDatabase>,
}

impl RoTransactionStub {
    fn get_database(&self, database: LmdbDatabase) -> Option<&ConfiguredDatabase> {
        self.databases.iter().find(|d| d.dbi == database)
    }

    fn reset(self) -> NullInactiveTransaction
    where
        Self: Sized,
    {
        NullInactiveTransaction {
            databases: self.databases,
        }
    }

    fn get(&self, database: LmdbDatabase, key: &[u8]) -> lmdb::Result<&[u8]> {
        let Some(db) = self.get_database(database) else {
            return Err(lmdb::Error::NotFound);
        };
        match db.entries.get(key) {
            Some(Ok(bytes)) => Ok(bytes.as_slice()),
            Some(Err(e)) => Err(*e),
            None => Err(lmdb::Error::NotFound),
        }
    }

    fn open_ro_cursor(&self, database: LmdbDatabase) -> lmdb::Result<RoCursor> {
        match self.get_database(database) {
            Some(db) => Ok(RoCursor::new_null_with(db)),
            None => Ok(RoCursor::new_null_with(&EMPTY_DATABASE)),
        }
    }

    fn count(&self, database: LmdbDatabase) -> u64 {
        self.get_database(database)
            .map(|db| db.entries.len())
            .unwrap_or_default() as u64
    }
}

pub struct InactiveTransaction {
    strategy: InactiveTransactionStrategy,
}

enum InactiveTransactionStrategy {
    Real(InactiveTransactionWrapper),
    Nulled(NullInactiveTransaction),
}

impl InactiveTransaction {
    pub fn renew(self) -> lmdb::Result<ReadTransaction> {
        match self.strategy {
            InactiveTransactionStrategy::Real(s) => s.renew(),
            InactiveTransactionStrategy::Nulled(s) => s.renew(),
        }
    }
}

pub struct InactiveTransactionWrapper {
    inactive: lmdb::InactiveTransaction<'static>,
}

impl InactiveTransactionWrapper {
    fn renew(self) -> lmdb::Result<ReadTransaction> {
        self.inactive.renew().map(ReadTransaction::new)
    }
}

pub struct NullInactiveTransaction {
    databases: Vec<ConfiguredDatabase>,
}

impl NullInactiveTransaction {
    fn renew(self) -> lmdb::Result<ReadTransaction> {
        Ok(ReadTransaction::new_null(self.databases))
    }
}
