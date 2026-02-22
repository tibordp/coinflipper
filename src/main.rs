mod flipper;
mod persistence;
mod server;
mod stats;
mod status;

pub mod pb {
    tonic::include_proto!("coinflipper");
}

use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(name = "coinflipper")]
#[command(about = "Coinflipper - Distributed coin flipping for RNG benchmarking")]
#[command(version = "1.0.0")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Run coin flipping server
    Server {
        /// Path for local filesystem storage (default: current directory)
        #[arg(long, conflicts_with = "s3_bucket")]
        storage_path: Option<String>,

        /// S3 bucket name for remote storage
        #[arg(long)]
        s3_bucket: Option<String>,

        /// S3 key prefix (e.g. "coinflipper/")
        #[arg(long, requires = "s3_bucket")]
        s3_prefix: Option<String>,
    },

    /// Run coin flipping worker
    Flipper {
        /// Server address (host or host:port)
        server: String,

        /// Number of worker threads
        #[arg(short = 'j', long = "threads", default_value_t = 0)]
        threads: usize,
    },

    /// Query server status
    Status {
        /// Server address (host or host:port)
        server: String,
    },

    /// Export server data
    Export {
        /// Server address (host or host:port)
        server: String,
    },
}

#[tokio::main]
async fn main() -> Result<(), anyhow::Error> {
    let cli = Cli::parse();

    match cli.command {
        Commands::Server {
            storage_path,
            s3_bucket,
            s3_prefix,
        } => {
            let backend: Box<dyn persistence::PersistenceBackend> = if let Some(bucket) = s3_bucket
            {
                let prefix = s3_prefix.unwrap_or_default();
                eprintln!("Using S3 backend: s3://{}/{}", bucket, prefix);
                Box::new(persistence::S3Backend::new(bucket, prefix).await)
            } else {
                let path = storage_path.unwrap_or_else(|| ".".to_string());
                eprintln!("Using filesystem backend: {}", path);
                Box::new(persistence::FilesystemBackend::new(path))
            };
            server::coin_server(backend).await?;
        }
        Commands::Flipper { server, threads } => {
            let thread_count = if threads == 0 {
                std::thread::available_parallelism()
                    .map(|n| n.get())
                    .unwrap_or(1)
            } else {
                threads
            };
            eprintln!("Starting {} worker threads", thread_count);
            flipper::coin_flipper(server, thread_count).await?;
        }
        Commands::Status { server } => {
            status::coin_status(server, false).await?;
        }
        Commands::Export { server } => {
            status::coin_status(server, true).await?;
        }
    }

    Ok(())
}
