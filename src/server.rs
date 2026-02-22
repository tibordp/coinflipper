use std::sync::Arc;
use std::time::Duration;

use prost::Message;
use tonic::{Request, Response, Status};

use crate::pb;
use crate::pb::coin_flipper_server::{CoinFlipper, CoinFlipperServer};
use crate::persistence::PersistenceBackend;
use crate::stats::{
    result_array_from_pb, result_array_to_pb, AsyncResults, AsyncStatistics,
};

struct CoinFlipperService {
    results: Arc<AsyncResults>,
    stats: Arc<AsyncStatistics>,
}

#[tonic::async_trait]
impl CoinFlipper for CoinFlipperService {
    async fn submit_batch(
        &self,
        request: Request<pb::Coinbatch>,
    ) -> Result<Response<pb::SubmitResponse>, Status> {
        let batch = request.into_inner();
        let arr = result_array_from_pb(&batch.flips);
        self.results.push(&arr, batch.total_flips as u64);
        self.stats.push(batch.hash, batch.total_flips as u64);
        Ok(Response::new(pb::SubmitResponse {}))
    }

    async fn get_status(
        &self,
        _request: Request<pb::StatusRequest>,
    ) -> Result<Response<pb::Coinstatus>, Status> {
        let (arr, total) = self.results.get();
        let tally = self.stats.get_tally();

        let total_speed: u64 = tally.values().map(|e| e.speed()).sum();

        let stats: Vec<pb::Coinstats> = tally
            .iter()
            .map(|(&hash, entry)| pb::Coinstats {
                hash,
                flips_per_second: entry.speed() as i64,
            })
            .collect();

        let status = pb::Coinstatus {
            flips: result_array_to_pb(&arr),
            total_flips: total as i64,
            flips_per_second: total_speed as f64,
            stats,
        };

        Ok(Response::new(status))
    }
}

fn encode_status(results: &Arc<AsyncResults>) -> Vec<u8> {
    let (arr, total) = results.get();
    let status = pb::Coinstatus {
        flips: result_array_to_pb(&arr),
        total_flips: total as i64,
        flips_per_second: 0.0,
        stats: vec![],
    };
    status.encode_to_vec()
}

pub async fn coin_server(
    backend: Box<dyn PersistenceBackend>,
) -> Result<(), Box<dyn std::error::Error>> {
    let results = Arc::new(AsyncResults::new());
    let stats = Arc::new(AsyncStatistics::new(Duration::from_secs(10)));

    // Load previous state
    if let Some(data) = backend.load().await {
        if let Ok(status) = pb::Coinstatus::decode(&data[..]) {
            let arr = result_array_from_pb(&status.flips);
            eprintln!("Loaded previous state ({} total flips)", status.total_flips);
            results.push(&arr, status.total_flips as u64);
        }
    }

    // Persistence task
    let persist_results = Arc::clone(&results);
    let backend: Arc<dyn PersistenceBackend> = Arc::from(backend);
    let persist_backend = Arc::clone(&backend);
    tokio::spawn(async move {
        loop {
            let data = encode_status(&persist_results);

            // Save history snapshot
            let now = time::OffsetDateTime::now_utc();
            let format = time::format_description::parse(
                "[year]_[month]_[day]_[hour]_[minute]_[second]",
            )
            .unwrap();
            let timestamp = now.format(&format).unwrap();

            if let Err(e) = persist_backend.save_snapshot(&timestamp, &data).await {
                eprintln!("Failed to save snapshot: {}", e);
            }

            // Save current status
            if let Err(e) = persist_backend.save(&data).await {
                eprintln!("Failed to save status: {}", e);
            }

            tokio::time::sleep(Duration::from_secs(300)).await;
        }
    });

    let addr = "[::]:50051".parse()?;
    eprintln!("Server listening on {}", addr);

    let service = CoinFlipperService {
        results,
        stats,
    };

    tonic::transport::Server::builder()
        .add_service(CoinFlipperServer::new(service))
        .serve(addr)
        .await?;

    Ok(())
}
