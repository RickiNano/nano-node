use std::{cmp::Ordering, marker::PhantomData, ops::Bound};

use rsnano_nullable_lmdb::{
    EMPTY_DATABASE, Error, Result, RoCursor,
    sys::{MDB_FIRST, MDB_LAST, MDB_NEXT, MDB_PREV, MDB_SET_RANGE, MDB_cursor_op},
};
use rsnano_types::stream::{BufferReader, Deserialize, MutStreamAdapter, Serialize};

pub struct LmdbRangeIterator<'txn, K, V> {
    cursor: RoCursor<'txn>,
    start: Bound<K>,
    end: Bound<K>,
    start2: Bound<Vec<u8>>,
    end2: Bound<Vec<u8>>,
    initialized: bool,
    empty: bool,
    phantom: PhantomData<(K, V)>,
}

impl<'txn, K, V> LmdbRangeIterator<'txn, K, V>
where
    K: Deserialize<Target = K> + Serialize + Ord,
    V: Deserialize<Target = V>,
{
    pub fn new(
        cursor: RoCursor<'txn>,
        start: Bound<K>,
        end: Bound<K>,
        start2: Bound<Vec<u8>>,
        end2: Bound<Vec<u8>>,
    ) -> Self {
        Self {
            cursor,
            start,
            end,
            start2,
            end2,
            initialized: false,
            empty: false,
            phantom: Default::default(),
        }
    }

    pub fn empty() -> Self {
        Self {
            cursor: RoCursor::new_null_with(&EMPTY_DATABASE),
            start: Bound::Unbounded,
            end: Bound::Unbounded,
            start2: Bound::Unbounded,
            end2: Bound::Unbounded,
            initialized: false,
            empty: true,
            phantom: Default::default(),
        }
    }

    fn get_next_result(&mut self) -> Result<(Option<&'txn [u8]>, &'txn [u8])> {
        if self.empty {
            Err(Error::NotFound)
        } else if !self.initialized {
            self.initialized = true;
            self.get_first_result()
        } else {
            self.cursor.get(None, None, MDB_NEXT)
        }
    }

    fn get_first_result(&self) -> Result<(Option<&'txn [u8]>, &'txn [u8])> {
        match &self.start {
            Bound::Included(start) => {
                let mut key_bytes = [0u8; 64];
                let mut stream = MutStreamAdapter::new(&mut key_bytes);
                start.serialize(&mut stream);
                self.cursor.get(Some(stream.written()), None, MDB_SET_RANGE)
            }
            Bound::Excluded(_) => unimplemented!(),
            Bound::Unbounded => self.cursor.get(None, None, MDB_FIRST),
        }
    }

    fn deserialize(&self, key_bytes: Option<&[u8]>, value_bytes: &[u8]) -> (K, V) {
        let mut stream = BufferReader::new(key_bytes.unwrap());
        let key = K::deserialize(&mut stream).unwrap();
        let mut stream = BufferReader::new(value_bytes);
        let value = V::deserialize(&mut stream).unwrap();
        (key, value)
    }

    fn should_include(&self, key: &K) -> bool {
        match &self.end {
            Bound::Included(end) => {
                matches!(key.cmp(end), Ordering::Less | Ordering::Equal)
            }
            Bound::Excluded(end) => matches!(key.cmp(end), Ordering::Less),
            Bound::Unbounded => true,
        }
    }
}

impl<'txn, K, V> Iterator for LmdbRangeIterator<'txn, K, V>
where
    K: Deserialize<Target = K> + Serialize + Ord,
    V: Deserialize<Target = V>,
{
    type Item = (K, V);

    fn next(&mut self) -> Option<Self::Item> {
        match self.get_next_result() {
            Ok((key, value)) => {
                let result = self.deserialize(key, value);
                if self.should_include(&result.0) {
                    Some(result)
                } else {
                    None
                }
            }
            Err(Error::NotFound) => None,
            Err(e) => panic!("Could not read from cursor: {:?}", e),
        }
    }
}

pub struct LmdbIterator<'txn, K, V> {
    cursor: RoCursor<'txn>,
    operation: MDB_cursor_op,
    next_op: MDB_cursor_op,
    convert: fn(&[u8], &[u8]) -> (K, V),
}

impl<'txn, K, V> LmdbIterator<'txn, K, V> {
    pub fn new(cursor: RoCursor<'txn>, convert: fn(&[u8], &[u8]) -> (K, V)) -> Self {
        Self {
            cursor,
            operation: MDB_FIRST,
            next_op: MDB_NEXT,
            convert,
        }
    }

    pub fn new_descending(cursor: RoCursor<'txn>, convert: fn(&[u8], &[u8]) -> (K, V)) -> Self {
        Self {
            cursor,
            operation: MDB_LAST,
            next_op: MDB_PREV,
            convert,
        }
    }
}

impl<'txn, K, V> Iterator for LmdbIterator<'txn, K, V> {
    type Item = (K, V);

    fn next(&mut self) -> Option<Self::Item> {
        let result = match self.cursor.get(None, None, self.operation) {
            Err(Error::NotFound) => None,
            Ok((Some(k), v)) => Some((self.convert)(k, v)),
            Ok(_) => panic!("No key returned"),
            Err(e) => panic!("Read error {:?}", e),
        };
        self.operation = self.next_op;
        result
    }
}
