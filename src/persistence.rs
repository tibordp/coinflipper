use std::path::PathBuf;

use tokio::fs;

#[async_trait::async_trait]
pub trait PersistenceBackend: Send + Sync + 'static {
    async fn load(&self) -> Option<Vec<u8>>;
    async fn save(&self, data: &[u8]) -> Result<(), Box<dyn std::error::Error>>;
    async fn save_snapshot(&self, timestamp: &str, data: &[u8])
        -> Result<(), Box<dyn std::error::Error>>;
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
    async fn load(&self) -> Option<Vec<u8>> {
        fs::read(self.dir.join("status.cf")).await.ok()
    }

    async fn save(&self, data: &[u8]) -> Result<(), Box<dyn std::error::Error>> {
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
    ) -> Result<(), Box<dyn std::error::Error>> {
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
    async fn load(&self) -> Option<Vec<u8>> {
        let result = self
            .client
            .get_object()
            .bucket(&self.bucket)
            .key(self.key("status.cf"))
            .send()
            .await
            .ok()?;

        result.body.collect().await.ok().map(|b| b.to_vec())
    }

    async fn save(&self, data: &[u8]) -> Result<(), Box<dyn std::error::Error>> {
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
    ) -> Result<(), Box<dyn std::error::Error>> {
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
