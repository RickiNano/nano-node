use std::{cmp::Ordering, ops::Bound};

use rsnano_nullable_lmdb::{
    EMPTY_DATABASE, Error, Result, RoCursor,
    sys::{MDB_FIRST, MDB_LAST, MDB_NEXT, MDB_PREV, MDB_SET_RANGE, MDB_cursor_op},
};

pub struct LmdbRangeIterator<'txn, K, V> {
    cursor: RoCursor<'txn>,
    start: Bound<Vec<u8>>,
    end: Bound<Vec<u8>>,
    initialized: bool,
    empty: bool,
    convert: fn(&[u8], &[u8]) -> (K, V),
}

impl<'txn, K, V> LmdbRangeIterator<'txn, K, V> {
    pub fn new(
        cursor: RoCursor<'txn>,
        start: Bound<Vec<u8>>,
        end: Bound<Vec<u8>>,
        convert: fn(&[u8], &[u8]) -> (K, V),
    ) -> Self {
        Self {
            cursor,
            start,
            end,
            initialized: false,
            empty: false,
            convert,
        }
    }

    pub fn empty(convert: fn(&[u8], &[u8]) -> (K, V)) -> Self {
        Self {
            cursor: RoCursor::new_null_with(&EMPTY_DATABASE),
            start: Bound::Unbounded,
            end: Bound::Unbounded,
            initialized: false,
            empty: true,
            convert,
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
            Bound::Included(start) => self.cursor.get(Some(start), None, MDB_SET_RANGE),
            Bound::Excluded(_) => unimplemented!(),
            Bound::Unbounded => self.cursor.get(None, None, MDB_FIRST),
        }
    }

    fn should_include(&self, key: &[u8]) -> bool {
        match &self.end {
            Bound::Included(end) => {
                matches!(key.cmp(end), Ordering::Less | Ordering::Equal)
            }
            Bound::Excluded(end) => matches!(key.cmp(end), Ordering::Less),
            Bound::Unbounded => true,
        }
    }
}

impl<'txn, K, V> Iterator for LmdbRangeIterator<'txn, K, V> {
    type Item = (K, V);

    fn next(&mut self) -> Option<Self::Item> {
        match self.get_next_result() {
            Ok((key, value)) => {
                let key = key.expect("Key should exist");
                if self.should_include(key) {
                    let result = (self.convert)(key, value);
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
