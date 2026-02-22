use std::collections::BTreeMap;
use std::sync::Mutex;
use std::time::{Duration, Instant};

use crate::pb;

pub type ResultArray = [u64; 128];

pub fn result_array_from_pb(flips: &[pb::Coinflip]) -> ResultArray {
    let mut result = [0u64; 128];
    for flip in flips {
        if (flip.position as usize) < 128 {
            result[flip.position as usize] = flip.flips;
        }
    }
    result
}

pub fn result_array_to_pb(result: &ResultArray) -> Vec<pb::Coinflip> {
    result
        .iter()
        .enumerate()
        .filter(|(_, &v)| v != 0)
        .map(|(i, &v)| pb::Coinflip {
            position: i as u32,
            flips: v,
        })
        .collect()
}

pub struct AsyncResults {
    inner: Mutex<(ResultArray, u64)>,
}

impl AsyncResults {
    pub fn new() -> Self {
        Self {
            inner: Mutex::new(([0u64; 128], 0)),
        }
    }

    pub fn push(&self, val: &ResultArray, count: u64) {
        let mut guard = self.inner.lock().unwrap();
        guard.1 += count;
        for i in 0..128 {
            guard.0[i] += val[i];
        }
    }

    pub fn get(&self) -> (ResultArray, u64) {
        let guard = self.inner.lock().unwrap();
        (guard.0, guard.1)
    }

    pub fn pop(&self) -> (ResultArray, u64) {
        let mut guard = self.inner.lock().unwrap();
        let result = (guard.0, guard.1);
        guard.0 = [0u64; 128];
        guard.1 = 0;
        result
    }
}

struct CoinPush {
    hash: i64,
    time: Instant,
    count: u64,
}

pub struct TallyEntry {
    pub total_coins: u64,
    pub begin: Instant,
    pub end: Instant,
}

impl TallyEntry {
    pub fn speed(&self) -> u64 {
        let duration = self.end.duration_since(self.begin);
        if duration.is_zero() {
            0
        } else {
            (self.total_coins as f64 / duration.as_secs_f64()) as u64
        }
    }
}

pub struct AsyncStatistics {
    pushes: Mutex<Vec<CoinPush>>,
    timeout: Duration,
}

impl AsyncStatistics {
    pub fn new(timeout: Duration) -> Self {
        Self {
            pushes: Mutex::new(Vec::new()),
            timeout,
        }
    }

    pub fn push(&self, hash: i64, count: u64) {
        let mut guard = self.pushes.lock().unwrap();
        let now = Instant::now();
        let cutoff = now - self.timeout;
        guard.retain(|p| p.time >= cutoff);
        guard.push(CoinPush {
            hash,
            time: now,
            count,
        });
    }

    pub fn get_tally(&self) -> BTreeMap<i64, TallyEntry> {
        let mut guard = self.pushes.lock().unwrap();
        let now = Instant::now();
        let cutoff = now - self.timeout;
        guard.retain(|p| p.time >= cutoff);

        let mut clients: BTreeMap<i64, TallyEntry> = BTreeMap::new();
        for push in guard.iter() {
            let entry = clients.entry(push.hash).or_insert_with(|| TallyEntry {
                total_coins: 0,
                begin: push.time,
                end: push.time,
            });
            entry.end = push.time;
            entry.total_coins += push.count;
        }
        clients
    }
}
