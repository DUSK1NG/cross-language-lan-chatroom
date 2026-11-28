use std::{
    collections::HashMap,
    fs::{self, File},
    io::{Read, Seek, SeekFrom},
    path::Path,
    sync::{Arc, Mutex, PoisonError},
    time::UNIX_EPOCH,
};

use anyhow::{Context, Result, anyhow, ensure};
use lan_chat_protocol::ONLINE_CHUNK_BYTES;
use tokio::sync::{Mutex as AsyncMutex, OwnedMutexGuard, Semaphore};

use super::files;
use crate::store::Rejected;

pub(crate) const DEFAULT_FILE_IO_CONCURRENCY: usize = 4;

#[derive(Clone)]
pub(crate) struct BlockingPool {
    slots: Arc<Semaphore>,
}

impl BlockingPool {
    pub(crate) fn new(capacity: usize) -> Self {
        // A zero-capacity pool would reject every file command forever.
        Self {
            slots: Arc::new(Semaphore::new(capacity.max(1))),
        }
    }

    pub(crate) async fn run<F, R>(&self, work: F) -> Result<R>
    where
        F: FnOnce() -> R + Send + 'static,
        R: Send + 'static,
    {
        let permit = self
            .slots
            .clone()
            .try_acquire_owned()
            .map_err(|_| Rejected("服务器文件处理繁忙"))?;
        tokio::task::spawn_blocking(move || {
            let _permit = permit;
            work()
        })
        .await
        .map_err(|error| anyhow!("文件处理任务失败: {error}"))
    }
}

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) struct DigestCacheKey {
    id: String,
    size: u64,
    modified_ns: u64,
}

type FileLocks = Arc<Mutex<HashMap<String, Arc<AsyncMutex<()>>>>>;
type Digests = Arc<Mutex<HashMap<DigestCacheKey, String>>>;

const MAX_CACHED_DIGESTS: usize = 4096;

#[derive(Clone)]
pub(crate) struct FileIoService {
    pool: BlockingPool,
    locks: FileLocks,
    digests: Digests,
}

/// Holds one file's lock and drops its map entry once nobody else is waiting on it.
struct FileLock {
    id: String,
    locks: FileLocks,
    guard: Option<OwnedMutexGuard<()>>,
}

impl Drop for FileLock {
    fn drop(&mut self) {
        self.guard = None;
        let mut locks = self.locks.lock().unwrap_or_else(PoisonError::into_inner);
        if locks
            .get(&self.id)
            .is_some_and(|lock| Arc::strong_count(lock) == 1)
        {
            locks.remove(&self.id);
        }
    }
}

// One digest per file id, and a hard ceiling so the cache cannot grow without bound.
fn remember(digests: &Digests, key: DigestCacheKey, digest: String) {
    let mut digests = digests.lock().unwrap_or_else(PoisonError::into_inner);
    digests.retain(|cached, _| cached.id != key.id);
    if digests.len() >= MAX_CACHED_DIGESTS {
        digests.clear();
    }
    digests.insert(key, digest);
}

impl FileIoService {
    pub(crate) fn new(capacity: usize) -> Self {
        Self {
            pool: BlockingPool::new(capacity),
            locks: Arc::new(Mutex::new(HashMap::new())),
            digests: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    // Task 6 calls this after releasing the global Online state lock.
    #[allow(dead_code)]
    pub(crate) async fn read_segment(
        &self,
        id: &str,
        path: &Path,
        size: i64,
        offset: i64,
        expected: &str,
    ) -> Result<Vec<u8>> {
        let _file_lock = self.file_lock(id).await;
        let id = id.to_owned();
        let path = path.to_path_buf();
        let expected = expected.to_owned();
        let digests = Arc::clone(&self.digests);
        self.pool
            .run(move || {
                let key = metadata_key(&id, &path, size)?;
                let cached = digests
                    .lock()
                    .unwrap_or_else(PoisonError::into_inner)
                    .get(&key)
                    .cloned();
                let cache_miss = cached.is_none();
                let digest = match cached {
                    Some(digest) => digest,
                    None => files::checksum(&path, size)?,
                };
                ensure!(digest == expected, "在线附件校验不通过");
                let bytes = read_chunk(&path, size, offset)?;
                ensure!(
                    metadata_key(&id, &path, size)? == key,
                    "在线附件在处理过程中发生变化"
                );
                if cache_miss {
                    remember(&digests, key, digest);
                }
                Ok(bytes)
            })
            .await?
    }

    // Task 6 calls this after validating and reserving the upload in the database.
    #[allow(dead_code)]
    pub(crate) async fn finalize(&self, id: &str, path: &Path, size: i64) -> Result<String> {
        let _file_lock = self.file_lock(id).await;
        let id = id.to_owned();
        let path = path.to_path_buf();
        let digests = Arc::clone(&self.digests);
        self.pool
            .run(move || {
                let digest = files::finalize(&path, size)?;
                let key = metadata_key(&id, &path, size)?;
                remember(&digests, key, digest.clone());
                Ok(digest)
            })
            .await?
    }

    async fn file_lock(&self, id: &str) -> FileLock {
        let lock = self
            .locks
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .entry(id.to_owned())
            .or_insert_with(|| Arc::new(AsyncMutex::new(())))
            .clone();
        // Built before awaiting so a cancelled waiter still releases its map entry.
        let mut held = FileLock {
            id: id.to_owned(),
            locks: Arc::clone(&self.locks),
            guard: None,
        };
        held.guard = Some(lock.lock_owned().await);
        held
    }
}

fn metadata_key(id: &str, path: &Path, size: i64) -> Result<DigestCacheKey> {
    ensure!(size >= 0, "在线附件大小无效");
    let metadata = fs::metadata(path)?;
    ensure!(
        metadata.len() == size as u64,
        "在线附件大小与数据库记录不一致"
    );
    let modified_ns = metadata
        .modified()
        .context("读取在线附件修改时间失败")?
        .duration_since(UNIX_EPOCH)
        .map_err(|_| anyhow!("在线附件修改时间无效"))?
        .as_nanos();
    Ok(DigestCacheKey {
        id: id.to_owned(),
        size: metadata.len(),
        modified_ns: modified_ns
            .try_into()
            .map_err(|_| anyhow!("在线附件修改时间超出支持范围"))?,
    })
}

fn read_chunk(path: &Path, size: i64, offset: i64) -> Result<Vec<u8>> {
    ensure!(size >= 0, "在线附件大小无效");
    ensure!(offset >= 0 && offset <= size, "在线附件分块偏移无效");
    let mut file = File::open(path)?;
    ensure!(
        file.metadata()?.len() == size as u64,
        "在线附件大小与数据库记录不一致"
    );
    let mut bytes = vec![0; (size - offset).min(ONLINE_CHUNK_BYTES) as usize];
    file.seek(SeekFrom::Start(offset as u64))?;
    file.read_exact(&mut bytes)?;
    Ok(bytes)
}

#[cfg(test)]
mod tests {
    use std::{
        fs::{self, File, FileTimes},
        path::{Path, PathBuf},
        sync::{Arc, Mutex},
        time::{Duration, UNIX_EPOCH},
    };

    use anyhow::Result;
    use tokio::{sync::oneshot, time::timeout};
    use uuid::Uuid;

    use super::*;
    use crate::store::Rejected;

    const SEVEN_BYTES_SHA256: &str =
        "a99c07ce93703c7390589c5b007bd9a97a8b6de29e9a920d474d4f028ce2d42c";
    const ABC_SHA256: &str = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const ABD_SHA256: &str = "a52d159f262b2c6ddb724a61840befc36eb30c88877a4030b65cbe86298449c9";
    const HELLO_SHA256: &str = "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";

    fn temp_root(label: &str) -> Result<PathBuf> {
        let root = std::env::temp_dir().join(format!("lan-chat-io-{label}-{}", Uuid::new_v4()));
        fs::create_dir(&root)?;
        Ok(root)
    }

    fn set_modified(path: &Path, seconds: u64) -> Result<()> {
        let times = FileTimes::new().set_modified(UNIX_EPOCH + Duration::from_secs(seconds));
        File::options().write(true).open(path)?.set_times(times)?;
        Ok(())
    }

    fn cache_key(id: &str, path: &Path) -> Result<DigestCacheKey> {
        let metadata = fs::metadata(path)?;
        Ok(DigestCacheKey {
            id: id.into(),
            size: metadata.len(),
            modified_ns: metadata
                .modified()?
                .duration_since(UNIX_EPOCH)?
                .as_nanos()
                .try_into()?,
        })
    }

    #[tokio::test]
    async fn first_chunk_hash_is_cached_and_file_io_does_not_block_commands() -> Result<()> {
        let root = temp_root("io-cache")?;
        let path = root.join("file.bin");
        fs::write(&path, vec![7u8; 1024])?;
        let service = FileIoService::new(2);
        let first = service
            .read_segment("file-cache", &path, 1024, 0, SEVEN_BYTES_SHA256)
            .await?;
        let second = service
            .read_segment("file-cache", &path, 1024, 0, SEVEN_BYTES_SHA256)
            .await?;
        assert_eq!(first, second);
        assert_eq!(first, vec![7u8; 1024]);
        assert_eq!(service.digests.lock().unwrap().len(), 1);
        assert_eq!(
            service
                .digests
                .lock()
                .unwrap()
                .get(&cache_key("file-cache", &path)?)
                .map(String::as_str),
            Some(SEVEN_BYTES_SHA256)
        );
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[tokio::test(flavor = "multi_thread", worker_threads = 2)]
    async fn blocking_pool_rejects_over_capacity() -> Result<()> {
        let pool = BlockingPool::new(1);
        let (entered_tx, entered_rx) = oneshot::channel();
        let (release_tx, release_rx) = oneshot::channel();
        let release_rx = Arc::new(Mutex::new(Some(release_rx)));
        let first_pool = pool.clone();
        let first = tokio::spawn(async move {
            first_pool
                .run(move || {
                    entered_tx.send(()).unwrap();
                    release_rx
                        .lock()
                        .unwrap()
                        .take()
                        .unwrap()
                        .blocking_recv()
                        .unwrap();
                    1
                })
                .await
        });
        timeout(Duration::from_secs(5), entered_rx).await??;
        let error = timeout(Duration::from_secs(5), pool.run(|| 2))
            .await
            .expect("capacity rejection must not wait for a slot")
            .unwrap_err();
        assert!(error.downcast_ref::<Rejected>().is_some());
        release_tx.send(()).unwrap();
        assert_eq!(timeout(Duration::from_secs(5), first).await???, 1);
        assert_eq!(pool.run(|| 2).await?, 2);
        Ok(())
    }

    #[tokio::test]
    async fn metadata_changes_invalidate_cached_digests() -> Result<()> {
        let root = temp_root("io-metadata")?;
        let path = root.join("file.bin");
        fs::write(&path, b"abc")?;
        set_modified(&path, 1_700_000_000)?;
        let service = FileIoService::new(1);
        assert_eq!(
            service
                .read_segment("file-metadata", &path, 3, 0, ABC_SHA256)
                .await?,
            b"abc"
        );
        let old_key = cache_key("file-metadata", &path)?;

        fs::write(&path, b"abd")?;
        set_modified(&path, 1_700_000_001)?;
        assert_eq!(
            service
                .read_segment("file-metadata", &path, 3, 0, ABD_SHA256)
                .await?,
            b"abd"
        );
        let new_key = cache_key("file-metadata", &path)?;
        assert_ne!(old_key, new_key);
        let digests = service.digests.lock().unwrap();
        assert_eq!(digests.get(&new_key).map(String::as_str), Some(ABD_SHA256));
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[tokio::test]
    async fn cached_digests_are_checked_against_the_expected_hash() -> Result<()> {
        let root = temp_root("io-expected")?;
        let path = root.join("file.bin");
        fs::write(&path, b"abc")?;
        let service = FileIoService::new(1);
        service
            .read_segment("file-expected", &path, 3, 0, ABC_SHA256)
            .await?;
        let error = service
            .read_segment("file-expected", &path, 3, 0, ABD_SHA256)
            .await
            .unwrap_err();
        assert!(error.to_string().contains("校验不通过"));
        assert_eq!(service.digests.lock().unwrap().len(), 1);
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[tokio::test]
    async fn finalize_renames_the_file_and_caches_its_digest() -> Result<()> {
        let root = temp_root("io-finalize")?;
        let path = root.join("file.bin");
        let staging = path.with_extension("partial");
        fs::write(&staging, b"hello")?;
        let service = FileIoService::new(1);
        let digest = service.finalize("file-finalize", &path, 5).await?;
        assert_eq!(digest, HELLO_SHA256);
        assert_eq!(fs::read(&path)?, b"hello");
        assert!(!staging.exists());
        assert_eq!(
            service
                .digests
                .lock()
                .unwrap()
                .get(&cache_key("file-finalize", &path)?)
                .map(String::as_str),
            Some(HELLO_SHA256)
        );
        assert_eq!(
            service
                .read_segment("file-finalize", &path, 5, 0, &digest)
                .await?,
            b"hello"
        );
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[tokio::test]
    async fn file_locks_and_stale_digests_are_released() -> Result<()> {
        let root = temp_root("io-release")?;
        let path = root.join("file.bin");
        fs::write(&path, b"abc")?;
        set_modified(&path, 1_700_000_000)?;
        let service = FileIoService::new(1);
        service
            .read_segment("file-release", &path, 3, 0, ABC_SHA256)
            .await?;
        fs::write(&path, b"abd")?;
        set_modified(&path, 1_700_000_001)?;
        service
            .read_segment("file-release", &path, 3, 0, ABD_SHA256)
            .await?;
        assert!(
            service
                .read_segment("file-release", &path, 3, 4, ABD_SHA256)
                .await
                .is_err()
        );
        assert!(service.locks.lock().unwrap().is_empty());
        assert_eq!(service.digests.lock().unwrap().len(), 1);

        let held = service.file_lock("file-release").await;
        let waiting = service.clone();
        let waiter = tokio::spawn(async move { waiting.file_lock("file-release").await });
        tokio::task::yield_now().await;
        waiter.abort();
        assert!(waiter.await.is_err());
        assert_eq!(service.locks.lock().unwrap().len(), 1);
        drop(held);
        assert!(service.locks.lock().unwrap().is_empty());
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[tokio::test]
    async fn zero_capacity_pool_still_runs_work() -> Result<()> {
        assert_eq!(BlockingPool::new(0).run(|| 3).await?, 3);
        Ok(())
    }

    #[tokio::test]
    async fn read_segment_rejects_an_offset_past_the_end() -> Result<()> {
        let root = temp_root("io-offset")?;
        let path = root.join("file.bin");
        fs::write(&path, b"abc")?;
        let service = FileIoService::new(1);
        assert!(
            service
                .read_segment("file-offset", &path, 3, 4, ABC_SHA256)
                .await
                .is_err()
        );
        assert!(service.digests.lock().unwrap().is_empty());
        fs::remove_dir_all(root)?;
        Ok(())
    }
}
