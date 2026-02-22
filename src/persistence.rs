use std::path::PathBuf;

use tokio::fs;

#[async_trait::async_trait]
pub trait PersistenceBackend: Send + Sync + 'static {
    async fn load(&self) -> Result<Option<Vec<u8>>, anyhow::Error>;
    async fn save(&self, data: &[u8]) -> Result<(), anyhow::Error>;
    async fn save_snapshot(&self, timestamp: &str, data: &[u8])
        -> Result<(), anyhow::Error>;
}

pub struct FilesystemBackend {
    dir: PathBuf,
}

impl FilesystemBackend {
    pub fn new(dir: impl Into<PathBuf>) -> Self {
        Self { dir: dir.into() }
    }
}

#[async_trait::async_trait]
impl PersistenceBackend for FilesystemBackend {
    async fn load(&self) -> Result<Option<Vec<u8>>, anyhow::Error> {
        match fs::read(self.dir.join("status.cf")).await {
            Ok(data) => Ok(Some(data)),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(None),
            Err(e) => Err(e.into()),
        }
    }

    async fn save(&self, data: &[u8]) -> Result<(), anyhow::Error> {
        let tmp_path = self.dir.join("~status.cf");
        let final_path = self.dir.join("status.cf");
        fs::write(&tmp_path, data).await?;
        fs::rename(&tmp_path, &final_path).await?;
        Ok(())
    }

    async fn save_snapshot(
        &self,
        timestamp: &str,
        data: &[u8],
    ) -> Result<(), anyhow::Error> {
        let history_dir = self.dir.join("history");
        if !history_dir.exists() {
            let _ = fs::create_dir_all(&history_dir).await;
        }
        let filename = format!("status_{}.cf", timestamp);
        fs::write(history_dir.join(filename), data).await?;
        Ok(())
    }
}

pub struct S3Backend {
    client: aws_sdk_s3::Client,
    bucket: String,
    prefix: String,
}

impl S3Backend {
    pub async fn new(bucket: String, prefix: String) -> Self {
        let config = aws_config::load_defaults(aws_config::BehaviorVersion::latest()).await;
        let client = aws_sdk_s3::Client::new(&config);
        Self {
            client,
            bucket,
            prefix,
        }
    }

    fn key(&self, name: &str) -> String {
        format!("{}{}", self.prefix, name)
    }
}

#[async_trait::async_trait]
impl PersistenceBackend for S3Backend {
    async fn load(&self) -> Result<Option<Vec<u8>>, anyhow::Error> {
        let result = self
            .client
            .get_object()
            .bucket(&self.bucket)
            .key(self.key("status.cf"))
            .send()
            .await;

        match result {
            Ok(output) => {
                let data = output.body.collect().await?.to_vec();
                Ok(Some(data))
            }
            Err(sdk_err)
                if sdk_err
                    .as_service_error()
                    .map_or(false, |e| e.is_no_such_key()) =>
            {
                Ok(None)
            }
            Err(e) => Err(e.into()),
        }
    }

    async fn save(&self, data: &[u8]) -> Result<(), anyhow::Error> {
        self.client
            .put_object()
            .bucket(&self.bucket)
            .key(self.key("status.cf"))
            .body(data.to_vec().into())
            .send()
            .await?;
        Ok(())
    }

    async fn save_snapshot(
        &self,
        timestamp: &str,
        data: &[u8],
    ) -> Result<(), anyhow::Error> {
        let key = self.key(&format!("history/status_{}.cf", timestamp));
        self.client
            .put_object()
            .bucket(&self.bucket)
            .key(key)
            .body(data.to_vec().into())
            .send()
            .await?;
        Ok(())
    }
}
