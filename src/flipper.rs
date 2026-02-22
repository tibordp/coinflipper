use std::collections::VecDeque;
use std::sync::Arc;
use std::time::Duration;

use rand::RngCore;
use rand::SeedableRng;
use rand_isaac::Isaac64Rng;

use crate::pb;
use crate::pb::coin_flipper_client::CoinFlipperClient;
use crate::stats::{result_array_to_pb, AsyncResults, ResultArray};

fn flip_coins(results: Arc<AsyncResults>) {
    let mut rng = Isaac64Rng::from_entropy();

    const ITER_NUM: u32 = 0xffff;
    let mut current: ResultArray = [0u64; 128];
    let mut count: u32 = 0;
    let mut prev: bool = true;

    let mut iteration: u32 = ITER_NUM;
    loop {
        let word = rng.next_u64();

        for i in (0..64).rev() {
            let t = (word >> i) & 1 == 1;

            if t == prev {
                count += 1;
            } else {
                prev = t;
                if count <= 127 {
                    current[count as usize] += 1;
                }
                count = 0;
            }
        }

        iteration -= 1;
        if iteration == 0 {
            results.push(&current, ITER_NUM as u64 * 64);
            current = [0u64; 128];
            iteration = ITER_NUM;
        }
    }
}

async fn send_batches(
    server_address: String,
    results: Arc<AsyncResults>,
    hash: i64,
) {
    let mut retry_queue: VecDeque<pb::Coinbatch> = VecDeque::new();
    let mut client: Option<CoinFlipperClient<tonic::transport::Channel>> = None;
    let mut backoff = Duration::from_secs(1);

    loop {
        tokio::time::sleep(Duration::from_secs(1)).await;

        // Drain current results
        let (arr, total) = results.pop();
        if total > 0 {
            retry_queue.push_back(pb::Coinbatch {
                hash,
                flips: result_array_to_pb(&arr),
                total_flips: total as i64,
            });
        }

        if retry_queue.is_empty() {
            continue;
        }

        // Ensure connection
        if client.is_none() {
            let endpoint = format!("http://{}:50051", server_address);
            match CoinFlipperClient::connect(endpoint).await {
                Ok(c) => {
                    client = Some(c);
                    backoff = Duration::from_secs(1);
                }
                Err(e) => {
                    eprintln!("Connection failed: {} (retrying in {:?})", e, backoff);
                    tokio::time::sleep(backoff).await;
                    backoff = (backoff * 2).min(Duration::from_secs(30));
                    continue;
                }
            }
        }

        // Send all queued batches
        let c = client.as_mut().unwrap();
        while let Some(batch) = retry_queue.front() {
            match c.submit_batch(batch.clone()).await {
                Ok(_) => {
                    retry_queue.pop_front();
                }
                Err(e) => {
                    eprintln!("Send failed: {} (will retry)", e);
                    client = None;
                    break;
                }
            }
        }
    }
}

pub async fn coin_flipper(
    server_address: String,
    thread_count: usize,
) -> Result<(), Box<dyn std::error::Error>> {
    let results = Arc::new(AsyncResults::new());
    let hash = rand::random::<i64>();

    eprintln!(
        "Started flipping the coins (my hash is: {:x})",
        hash as u64
    );

    // Spawn worker threads
    for _ in 0..thread_count {
        let results = Arc::clone(&results);
        std::thread::spawn(move || flip_coins(results));
    }

    // Run sender in async context
    send_batches(server_address, results, hash).await;

    Ok(())
}
