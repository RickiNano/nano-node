use std::{cmp::Ordering, collections::BTreeMap};

use rsnano_core::{Block, BlockHash, HashOrAccount};

use super::{UncheckedInfo, UncheckedKey};

#[derive(Clone, Debug)]
pub(super) struct Entry {
    key: UncheckedKey,
    info: UncheckedInfo,
}

impl Entry {
    pub fn new(key: UncheckedKey, info: UncheckedInfo) -> Self {
        Self { key, info }
    }
}

impl PartialEq for Entry {
    fn eq(&self, other: &Self) -> bool {
        self.key.eq(&other.key)
    }
}

impl Eq for Entry {}

impl PartialOrd for Entry {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        self.key.partial_cmp(&other.key)
    }
}

impl Ord for Entry {
    fn cmp(&self, other: &Self) -> Ordering {
        self.key.cmp(&other.key)
    }
}

#[derive(Default, Clone, Debug)]
pub(super) struct UncheckedContainer {
    next_id: usize,
    by_key: BTreeMap<UncheckedKey, usize>,
    by_id: BTreeMap<usize, Entry>,
}

impl UncheckedContainer {
    pub fn new() -> Self {
        Self {
            by_id: BTreeMap::new(),
            by_key: BTreeMap::new(),
            next_id: 0,
        }
    }

    pub fn insert(&mut self, entry: Entry) -> bool {
        match self.by_key.get(&entry.key) {
            Some(_key) => false,
            None => {
                self.by_key.insert(entry.key.clone(), self.next_id);
                self.by_id.insert(self.next_id, entry);

                self.next_id = self.next_id.wrapping_add(1);

                true
            }
        }
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn remove(&mut self, key: &UncheckedKey) -> Option<Entry> {
        if let Some(id) = self.by_key.remove(key) {
            self.by_id.remove(&id)
        } else {
            None
        }
    }

    pub fn len(&self) -> usize {
        self.by_id.len()
    }

    pub fn pop_front(&mut self) -> Option<Entry> {
        if let Some((_id, entry)) = self.by_id.pop_first() {
            self.by_key.remove(&entry.key);
            Some(entry)
        } else {
            None
        }
    }

    pub fn clear(&mut self) {
        self.by_id.clear();
        self.by_key.clear();
        self.next_id = 0;
    }

    #[allow(dead_code)]
    pub fn exists(&self, key: &UncheckedKey) -> bool {
        self.by_key.contains_key(key)
    }

    pub fn for_each(
        &self,
        mut action: impl FnMut(&UncheckedKey, &Block),
        mut predicate: impl FnMut() -> bool,
    ) {
        for entry in self.by_id.values() {
            if !predicate() {
                break;
            }
            action(&entry.key, &entry.info.block);
        }
    }

    pub fn for_each_with_dependency(
        &self,
        dependency: &HashOrAccount,
        mut action: impl FnMut(&UncheckedKey, &Block),
        mut predicate: impl FnMut() -> bool,
    ) {
        let key = UncheckedKey::new(dependency.into(), BlockHash::zero());
        for (key, id) in self.by_key.range(key..) {
            if !predicate() || key.previous != dependency.into() {
                break;
            }
            let entry = self.by_id.get(id).unwrap();
            action(&entry.key, &entry.info.block);
        }
    }
}

#[cfg(test)]
mod tests {
    use rsnano_core::Block;

    use super::*;

    #[test]
    fn empty_container() {
        let container = UncheckedContainer::new();
        assert_eq!(container.next_id, 0);
        assert_eq!(container.by_id.len(), 0);
        assert_eq!(container.by_key.len(), 0);
    }

    #[test]
    fn insert_one_entry() {
        let mut container = UncheckedContainer::new();

        let entry = test_entry(1);
        let new_insert = container.insert(entry.clone());

        assert_eq!(container.next_id, 1);
        assert_eq!(container.by_id.len(), 1);
        assert_eq!(container.by_id.get(&0).unwrap(), &entry);
        assert_eq!(container.by_key.len(), 1);
        assert_eq!(container.by_key.get(&entry.key).unwrap(), &0);
        assert_eq!(new_insert, true);
    }

    #[test]
    fn insert_two_entries_with_same_key() {
        let mut container = UncheckedContainer::new();

        let entry = test_entry(1);
        let new_insert1 = container.insert(entry.clone());
        let new_insert2 = container.insert(entry);

        assert_eq!(container.next_id, 1);
        assert_eq!(container.by_id.len(), 1);
        assert_eq!(container.by_key.len(), 1);
        assert_eq!(new_insert1, true);
        assert_eq!(new_insert2, false);
    }

    #[test]
    fn insert_two_entries_with_different_key() {
        let mut container = UncheckedContainer::new();

        let new_insert1 = container.insert(test_entry(1));
        let new_insert2 = container.insert(test_entry(2));

        assert_eq!(container.next_id, 2);
        assert_eq!(container.by_id.len(), 2);
        assert_eq!(container.by_key.len(), 2);
        assert_eq!(new_insert1, true);
        assert_eq!(new_insert2, true);
    }

    #[test]
    fn pop_front() {
        let mut container = UncheckedContainer::new();

        container.insert(test_entry(1));
        let entry = test_entry(2);
        container.insert(entry.clone());

        container.pop_front();

        assert_eq!(container.next_id, 2);
        assert_eq!(container.by_id.len(), 1);
        assert_eq!(container.by_id.get(&1).is_some(), true);
        assert_eq!(container.by_key.len(), 1);
        assert_eq!(container.by_key.get(&entry.key).unwrap(), &1);
        assert_eq!(container.len(), 1);
    }

    #[test]
    fn pop_front_twice() {
        let mut container = UncheckedContainer::new();

        container.insert(test_entry(1));
        container.insert(test_entry(2));

        container.pop_front();
        container.pop_front();

        assert_eq!(container.len(), 0);
    }

    #[test]
    fn remove_by_key() {
        let mut container = UncheckedContainer::new();
        container.insert(test_entry(1));
        let entry = test_entry(2);
        container.insert(entry.clone());

        container.remove(&entry.key);

        assert_eq!(container.len(), 1);
        assert_eq!(container.by_id.len(), 1);
        assert_eq!(container.by_key.len(), 1);
        assert_eq!(container.exists(&entry.key), false);
    }

    fn test_entry<T: Into<BlockHash>>(hash: T) -> Entry {
        Entry::new(
            UncheckedKey::new(hash.into(), BlockHash::default()),
            UncheckedInfo::new(Block::new_test_instance()),
        )
    }
}
