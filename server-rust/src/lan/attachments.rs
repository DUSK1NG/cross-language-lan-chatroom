use std::{
    fs::{self, OpenOptions},
    io::{ErrorKind, Read, Write},
    path::{Path, PathBuf},
    sync::{Arc, MutexGuard},
};

use anyhow::{Context, Result, ensure};
use base64::{Engine, engine::general_purpose::STANDARD};
use chrono::{DateTime, SecondsFormat, Utc};
use lan_chat_protocol::{
    ATTACHMENT_CHUNK_BYTES, ATTACHMENT_CIPHER_OVERHEAD, ATTACHMENT_TAG_BYTES, MAX_ATTACHMENT_BYTES,
    MAX_ROOM_ATTACHMENT_BYTES, Message,
};
use ring::digest::{Context as Digest, SHA256, digest};
use rusqlite::{OptionalExtension, TransactionBehavior, params};
use uuid::Uuid;

use super::{Hub, Server};
use crate::store::Store;
use crate::store::{Rejected, now};
use tokio::sync::mpsc;

const MAX_CIPHER_BYTES: i64 = ATTACHMENT_CHUNK_BYTES + ATTACHMENT_CIPHER_OVERHEAD;
const INVALID_CHUNK: Rejected = Rejected("Invalid attachment chunk");
const INCOMPLETE: Rejected = Rejected("Attachment chunks are incomplete");

struct Upload {
    attachment: String,
    room: String,
    author: String,
    size: i64,
    expiry: String,
}

pub(super) struct Job {
    peer: Uuid,
    author: String,
    room: String,
    generation: Uuid,
    session: Uuid,
    request: Message,
}

pub(super) enum Work {
    Request(Box<Job>),
    Cleanup,
    #[cfg(test)]
    Pause(std::sync::mpsc::Sender<()>, std::sync::mpsc::Receiver<()>),
    #[cfg(test)]
    PausePublication(std::sync::mpsc::Sender<()>, std::sync::mpsc::Receiver<()>),
}

struct Attachments {
    store: Store,
    attachment_root: PathBuf,
    #[cfg(test)]
    pause: Option<(std::sync::mpsc::Sender<()>, std::sync::mpsc::Receiver<()>)>,
    #[cfg(test)]
    pause_publication: Option<(std::sync::mpsc::Sender<()>, std::sync::mpsc::Receiver<()>)>,
}

impl Job {
    fn authorize<'a>(&self, server: &'a Server) -> Result<MutexGuard<'a, Hub>> {
        let hub = server.hub.lock().expect("LAN hub poisoned");
        let denied = Rejected("Attachment access changed; retry from the current channel");
        let peer = hub.clients.get(&self.peer).ok_or(denied)?;
        ensure!(
            peer.identity.code == self.author
                && peer.room == self.room
                && peer.attachment_session == self.session
                && hub
                    .rooms
                    .get(&self.room)
                    .is_some_and(|room| room.generation == self.generation),
            denied
        );
        Ok(hub)
    }
}

impl Hub {
    pub(super) fn attachment(&mut self, id: Uuid, request: &Message) -> Result<()> {
        let peer = &self.clients[&id];
        let job = Job {
            peer: id,
            author: peer.identity.code.clone(),
            room: peer.room.clone(),
            generation: self.rooms[&peer.room].generation,
            session: peer.attachment_session,
            request: request.clone(),
        };
        self.attachments
            .try_send(Work::Request(Box::new(job)))
            .map_err(|error| match error {
                mpsc::error::TrySendError::Full(_) => Rejected("Attachment service is busy"),
                mpsc::error::TrySendError::Closed(_) => {
                    Rejected("Attachment service is unavailable")
                }
            })?;
        Ok(())
    }
}

pub(super) fn start(
    server: &Arc<Server>,
    database: &Path,
    mut jobs: mpsc::Receiver<Work>,
) -> Result<std::thread::JoinHandle<()>> {
    let mut service = Attachments {
        store: Store::lan_reader(database)?,
        attachment_root: database
            .parent()
            .unwrap_or(Path::new("."))
            .join("attachments"),
        #[cfg(test)]
        pause: None,
        #[cfg(test)]
        pause_publication: None,
    };
    service.cleanup_attachments(server)?;
    let server = Arc::downgrade(server);
    let worker = std::thread::Builder::new()
        .name("lan-attachments".into())
        .spawn(move || {
            while let Some(work) = jobs.blocking_recv() {
                let Some(server) = server.upgrade() else {
                    return;
                };
                match work {
                    Work::Request(job) => {
                        let result = service.process(&server, &job);
                        if let Err(error) = result {
                            let mut hub = server.hub.lock().expect("LAN hub poisoned");
                            hub.error(
                                job.peer,
                                &job.request,
                                crate::reason(&error, "局域网附件处理失败"),
                            );
                        }
                    }
                    #[cfg(test)]
                    Work::Pause(entered, resume) => {
                        service.pause = Some((entered, resume));
                    }
                    #[cfg(test)]
                    Work::PausePublication(entered, resume) => {
                        service.pause_publication = Some((entered, resume));
                    }
                    Work::Cleanup => {
                        if let Err(error) = service.cleanup_attachments(&server) {
                            eprintln!("局域网附件服务停止: {error:#}");
                            return;
                        }
                    }
                }
            }
        })?;
    Ok(worker)
}

impl Attachments {
    fn process(&mut self, server: &Server, job: &Job) -> Result<()> {
        drop(job.authorize(server)?);
        #[cfg(test)]
        if let Some((entered, resume)) = self.pause.take() {
            entered.send(()).expect("test observer");
            resume.recv().expect("test unblock");
        }
        let request = &job.request;
        let mut response = Message {
            kind: request.kind.clone(),
            command_id: request.command_id.clone(),
            ..Message::default()
        };
        if request.kind == "attachment.init" {
            ensure!(
                job.room == request.room,
                Rejected("Attachment upload access denied")
            );
            self.attachment_init(server, job, &mut response)?;
        } else if request.kind == "attachment.download" {
            self.attachment_download(request, &job.room, &mut response)?;
        } else {
            let upload = self.attachment_upload(&request.upload_id)?;
            ensure!(
                job.room == upload.room && job.author == upload.author,
                Rejected("Attachment upload access denied")
            );
            ensure!(
                DateTime::parse_from_rfc3339(&upload.expiry)? > Utc::now(),
                Rejected("Attachment upload expired")
            );
            match request.kind.as_str() {
                "attachment.chunk" => self.attachment_chunk(server, job, &upload, &mut response)?,
                "attachment.resume" => {
                    self.attachment_resume(server, job, &upload, &mut response)?
                }
                "attachment.commit" => {
                    self.attachment_commit(server, job, &upload, &mut response)?
                }
                _ => unreachable!("attachment dispatch"),
            }
        }
        job.authorize(server)?.deliver(job.peer, response);
        Ok(())
    }

    fn attachment_init(
        &mut self,
        server: &Server,
        job: &Job,
        response: &mut Message,
    ) -> Result<()> {
        let request = &job.request;
        let author = &job.author;
        let size = request.logical_size;
        ensure!(size > 0, Rejected("Invalid attachment size"));
        ensure!(
            size <= MAX_ATTACHMENT_BYTES,
            Rejected("attachment exceeds the 5 GiB limit")
        );
        let timestamp = Utc::now();
        let expires =
            (timestamp + chrono::Duration::hours(24)).to_rfc3339_opts(SecondsFormat::AutoSi, true);
        let created = timestamp.to_rfc3339_opts(SecondsFormat::AutoSi, true);
        let attachment = Uuid::new_v4().to_string();
        let upload = Uuid::new_v4().to_string();
        let mut hub = job.authorize(server)?;
        let tx = hub
            .store
            .0
            .transaction_with_behavior(TransactionBehavior::Immediate)?;
        let changed = tx.execute(
            "INSERT INTO room_quotas (room,reserved_bytes,used_bytes) VALUES (?,?,0)
             ON CONFLICT(room) DO UPDATE SET reserved_bytes=room_quotas.reserved_bytes+excluded.reserved_bytes
             WHERE room_quotas.reserved_bytes+room_quotas.used_bytes+excluded.reserved_bytes<=?",
            params![request.room, size, MAX_ROOM_ATTACHMENT_BYTES],
        )?;
        ensure!(changed == 1, Rejected("Room attachment quota exceeded"));
        tx.execute(
            "INSERT INTO attachments (attachment_id,room,logical_size,created_at) VALUES (?,?,?,?)",
            params![attachment, request.room, size, created],
        )?;
        tx.execute(
            "INSERT INTO attachment_uploads
             (upload_id,attachment_id,room,author_code,logical_size,reserved_bytes,chunk_size,expires_at,created_at)
             VALUES (?,?,?,?,?,?,?,?,?)",
            params![upload, attachment, request.room, author, size, size, ATTACHMENT_CHUNK_BYTES, expires, created],
        )?;
        tx.commit()?;
        drop(hub);
        response.room = request.room.clone();
        response.upload_id = upload;
        response.attachment_id = attachment;
        response.chunk_size = ATTACHMENT_CHUNK_BYTES;
        response.expires_at = expires;
        Ok(())
    }

    fn attachment_upload(&self, id: &str) -> Result<Upload> {
        let missing = Rejected("Attachment upload not found");
        ensure!(valid_id(id), missing);
        let (upload, chunk_size) = self.store.0.query_row(
            "SELECT attachment_id,room,author_code,logical_size,expires_at,chunk_size FROM attachment_uploads WHERE upload_id=?",
            [id],
            |row| Ok((Upload {
                attachment: row.get(0)?,
                room: row.get(1)?,
                author: row.get(2)?,
                size: row.get(3)?,
                expiry: row.get(4)?,
            }, row.get::<_, i64>(5)?)),
        ).optional()?.ok_or(missing)?;
        ensure!(
            valid_id(&upload.attachment)
                && (1..=MAX_ATTACHMENT_BYTES).contains(&upload.size)
                && chunk_size == ATTACHMENT_CHUNK_BYTES,
            "数据库中的上传元数据无效"
        );
        Ok(upload)
    }

    fn attachment_chunk(
        &mut self,
        server: &Server,
        job: &Job,
        upload: &Upload,
        response: &mut Message,
    ) -> Result<()> {
        let request = &job.request;
        let index = request.chunk_index.unwrap_or(0);
        ensure!(
            (0..chunk_count(upload.size)).contains(&index),
            INVALID_CHUNK
        );
        let data = STANDARD
            .decode(&request.ciphertext)
            .map_err(|_| INVALID_CHUNK)?;
        ensure!(
            (1..=MAX_CIPHER_BYTES).contains(&(data.len() as i64)),
            INVALID_CHUNK
        );
        let sha = hex(digest(&SHA256, &data).as_ref());
        ensure!(
            sha == request.cipher_sha256,
            Rejected("Attachment chunk hash mismatch")
        );
        let directory = self.upload_directory(request, upload)?;
        let path = chunk_path(&directory, index);
        let existing: Option<(i64, String)> = self.store.0.query_row(
            "SELECT cipher_size,cipher_sha256 FROM attachment_chunks WHERE upload_id=? AND chunk_index=?",
            params![request.upload_id, index],
            |row| Ok((row.get(0)?, row.get(1)?)),
        ).optional()?;
        let orphan = path.try_exists()?;
        if let Some((size, hash)) = &existing {
            ensure!(
                *size == data.len() as i64 && *hash == sha,
                Rejected("Attachment chunk rejected")
            );
            ensure!(
                read_chunk(&path)? == data,
                Rejected("Attachment chunk rejected")
            );
        } else {
            if orphan {
                ensure!(
                    read_chunk(&path)? == data,
                    Rejected("Attachment chunk rejected")
                );
            } else {
                write_chunk(&path, &data)?;
            }
            let mut hub = job.authorize(server)?;
            let tx = hub.store.0.transaction()?;
            tx.execute(
                "INSERT INTO attachment_chunks (upload_id,chunk_index,cipher_size,cipher_sha256,created_at) VALUES (?,?,?,?,?)",
                params![request.upload_id, index, data.len() as i64, sha, now()],
            )?;
            tx.commit()?;
        }
        response.upload_id = request.upload_id.clone();
        response.chunk_index = request.chunk_index;
        response.content = if existing.is_some() || orphan {
            "duplicate"
        } else {
            "stored"
        }
        .into();
        Ok(())
    }

    fn attachment_resume(
        &mut self,
        server: &Server,
        job: &Job,
        upload: &Upload,
        response: &mut Message,
    ) -> Result<()> {
        let request = &job.request;
        let directory = self.upload_directory(request, upload)?;
        let mut indexes = Vec::new();
        let mut invalid = Vec::new();
        for (index, size, sha) in stored_chunks(&self.store.0, &request.upload_id)? {
            ensure!(
                (0..chunk_count(upload.size)).contains(&index),
                "数据库中的分块编号超出范围"
            );
            let path = chunk_path(&directory, index);
            let valid = match read_chunk(&path) {
                Ok(data) => {
                    data.len() as i64 == size && hex(digest(&SHA256, &data).as_ref()) == sha
                }
                Err(error) if error.kind() == ErrorKind::NotFound => false,
                Err(error) => return Err(error.into()),
            };
            if valid {
                indexes.push(index);
            } else {
                if path.try_exists()? {
                    fs::remove_file(path)?;
                }
                invalid.push(index);
            }
        }
        if !invalid.is_empty() {
            let mut hub = job.authorize(server)?;
            let tx = hub.store.0.transaction()?;
            for index in invalid {
                tx.execute(
                    "DELETE FROM attachment_chunks WHERE upload_id=? AND chunk_index=?",
                    params![request.upload_id, index],
                )?;
            }
            tx.commit()?;
        }
        response.upload_id = request.upload_id.clone();
        if indexes.len() > 8192 {
            let mut bitmap = vec![0; *indexes.last().expect("nonempty indexes") as usize / 8 + 1];
            for index in indexes {
                bitmap[index as usize / 8] |= 1 << (index % 8);
            }
            response.received_bitmap = hex(&bitmap);
        } else {
            response.received_indexes = indexes;
        }
        Ok(())
    }

    fn attachment_commit(
        &mut self,
        server: &Server,
        job: &Job,
        upload: &Upload,
        response: &mut Message,
    ) -> Result<()> {
        let request = &job.request;
        let partial = self.partial_dir(&request.upload_id);
        let published = self.attachment_root.join(&upload.attachment);
        let already_moved = published.try_exists()?;
        let directory = if already_moved { &published } else { &partial };
        let mut whole = Digest::new(&SHA256);
        let chunks = stored_chunks(&self.store.0, &request.upload_id)?;
        ensure!(chunks.len() as i64 == chunk_count(upload.size), INCOMPLETE);
        for (expected, (index, size, sha)) in chunks.into_iter().enumerate() {
            ensure!(
                expected as i64 == index && size == expected_cipher_size(upload.size, index),
                INCOMPLETE
            );
            let data = read_chunk(&chunk_path(directory, index))
                .with_context(|| format!("读取附件分块 {index}"))?;
            ensure!(
                data.len() as i64 == size && hex(digest(&SHA256, &data).as_ref()) == sha,
                "附件分块 {index} 完整性检查失败"
            );
            whole.update(&data);
        }
        let whole = hex(whole.finish().as_ref());
        // Upload metadata remains available until the verified directory is published.
        drop(job.authorize(server)?);
        if !already_moved {
            fs::rename(&partial, &published).context("发布已验证的附件")?;
        }
        #[cfg(test)]
        if let Some((entered, resume)) = self.pause_publication.take() {
            entered.send(()).expect("test observer");
            resume.recv().expect("test unblock");
        }
        let mut hub = job.authorize(server)?;
        ensure!(
            DateTime::parse_from_rfc3339(&upload.expiry)? > Utc::now(),
            Rejected("Attachment upload expired")
        );
        let tx = hub
            .store
            .0
            .transaction_with_behavior(TransactionBehavior::Immediate)?;
        let rows = tx.execute(
            "UPDATE attachments SET whole_sha256=?,committed_at=? WHERE attachment_id=? AND committed_at IS NULL",
            params![whole, now(), upload.attachment],
        )?;
        ensure!(rows == 1, Rejected("Attachment already committed"));
        tx.execute(
            "UPDATE room_quotas SET reserved_bytes=reserved_bytes-?1,used_bytes=used_bytes+?1 WHERE room=?2",
            params![upload.size, upload.room],
        )?;
        tx.execute(
            "DELETE FROM attachment_chunks WHERE upload_id=?",
            [&request.upload_id],
        )?;
        tx.execute(
            "DELETE FROM attachment_uploads WHERE upload_id=?",
            [&request.upload_id],
        )?;
        tx.commit()?;
        response.attachment_id = upload.attachment.clone();
        response.logical_size = upload.size;
        response.whole_sha256 = whole;
        response.content = "committed".into();
        Ok(())
    }

    fn attachment_download(
        &self,
        request: &Message,
        peer_room: &str,
        response: &mut Message,
    ) -> Result<()> {
        let denied = Rejected("Attachment download access denied");
        ensure!(valid_id(&request.attachment_id), denied);
        let size: i64 = self.store.0.query_row(
            "SELECT logical_size FROM attachments WHERE attachment_id=? AND room=? AND committed_at IS NOT NULL",
            params![request.attachment_id, peer_room],
            |row| row.get(0),
        ).optional()?.ok_or(denied)?;
        ensure!(
            (1..=MAX_ATTACHMENT_BYTES).contains(&size),
            "数据库中的附件大小无效"
        );
        let index = request.chunk_index.unwrap_or(0);
        ensure!(
            (0..chunk_count(size)).contains(&index),
            Rejected("Attachment download failed")
        );
        let path = chunk_path(&self.attachment_root.join(&request.attachment_id), index);
        let data = read_chunk(&path)?;
        ensure!(
            data.len() as i64 == expected_cipher_size(size, index),
            "附件分块 {index} 长度无效"
        );
        response.attachment_id = request.attachment_id.clone();
        response.chunk_index = request.chunk_index;
        response.cipher_sha256 = hex(digest(&SHA256, &data).as_ref());
        response.ciphertext = STANDARD.encode(data);
        response.content = if index + 1 == chunk_count(size) {
            "last"
        } else {
            "chunk"
        }
        .into();
        Ok(())
    }

    fn cleanup_attachments(&mut self, server: &Server) -> Result<()> {
        let expired = {
            let mut statement = self.store.0.prepare(
                "SELECT upload_id,attachment_id,room,reserved_bytes FROM attachment_uploads
                 WHERE julianday(expires_at)<=julianday('now')",
            )?;
            statement
                .query_map([], |row| {
                    Ok((
                        row.get::<_, String>(0)?,
                        row.get::<_, String>(1)?,
                        row.get::<_, String>(2)?,
                        row.get::<_, i64>(3)?,
                    ))
                })?
                .collect::<rusqlite::Result<Vec<_>>>()?
        };
        for (upload, attachment, room, reserved) in expired {
            ensure!(
                valid_id(&upload) && valid_id(&attachment),
                "数据库中的上传或附件编号无效"
            );
            // Keeping metadata until disk cleanup completes makes interruption retryable.
            for directory in [
                self.partial_dir(&upload),
                self.attachment_root.join(&attachment),
            ] {
                if directory.try_exists()? {
                    fs::remove_dir_all(directory)?;
                }
            }
            let mut hub = server.hub.lock().expect("LAN hub poisoned");
            let tx = hub
                .store
                .0
                .transaction_with_behavior(TransactionBehavior::Immediate)?;
            tx.execute("DELETE FROM attachment_chunks WHERE upload_id=?", [&upload])?;
            let removed = tx.execute(
                "DELETE FROM attachment_uploads WHERE upload_id=?",
                [&upload],
            )?;
            tx.execute(
                "DELETE FROM attachments WHERE attachment_id=? AND committed_at IS NULL",
                [&attachment],
            )?;
            if removed == 1 {
                tx.execute(
                    "UPDATE room_quotas SET reserved_bytes=reserved_bytes-? WHERE room=?",
                    params![reserved, room],
                )?;
            }
            tx.commit()?;
        }
        self.cleanup_orphans()?;
        Ok(())
    }

    fn cleanup_orphans(&self) -> Result<()> {
        for (directory, table, column) in [
            (self.attachment_root.clone(), "attachments", "attachment_id"),
            (
                self.attachment_root.join(".partial"),
                "attachment_uploads",
                "upload_id",
            ),
        ] {
            if !directory.try_exists()? {
                continue;
            }
            for entry in fs::read_dir(directory)? {
                let entry = entry?;
                if !entry.file_type()?.is_dir() {
                    continue;
                }
                let name = entry.file_name();
                let Some(id) = name.to_str().filter(|name| valid_id(name)) else {
                    continue;
                };
                let exists: bool = self.store.0.query_row(
                    &format!("SELECT EXISTS(SELECT 1 FROM {table} WHERE {column}=?)"),
                    [id],
                    |row| row.get(0),
                )?;
                if !exists {
                    fs::remove_dir_all(entry.path())?;
                }
            }
        }
        Ok(())
    }

    fn upload_directory(&self, request: &Message, upload: &Upload) -> Result<PathBuf> {
        let published = self.attachment_root.join(&upload.attachment);
        if published.try_exists()? {
            Ok(published)
        } else {
            Ok(self.partial_dir(&request.upload_id))
        }
    }

    fn partial_dir(&self, upload: &str) -> PathBuf {
        self.attachment_root.join(".partial").join(upload)
    }
}

fn valid_id(id: &str) -> bool {
    Uuid::parse_str(id).is_ok_and(|value| value.to_string() == id)
}

fn chunk_count(size: i64) -> i64 {
    (size - 1) / ATTACHMENT_CHUNK_BYTES + 1
}

fn expected_cipher_size(size: i64, index: i64) -> i64 {
    (size - index * ATTACHMENT_CHUNK_BYTES).min(ATTACHMENT_CHUNK_BYTES) + ATTACHMENT_TAG_BYTES
}

fn chunk_path(dir: &Path, index: i64) -> PathBuf {
    dir.join(format!("{index}.bin"))
}

fn read_chunk(path: &Path) -> std::io::Result<Vec<u8>> {
    let mut data = Vec::new();
    fs::File::open(path)?
        .take(MAX_CIPHER_BYTES as u64 + 1)
        .read_to_end(&mut data)?;
    Ok(data)
}

fn write_chunk(path: &Path, data: &[u8]) -> Result<()> {
    let directory = path.parent().expect("chunk path includes upload directory");
    fs::create_dir_all(directory)?;
    let temporary = directory.join(format!(".chunk-{}", Uuid::new_v4()));
    let mut file = OpenOptions::new()
        .create_new(true)
        .write(true)
        .open(&temporary)?;
    let result = file.write_all(data).and_then(|()| file.sync_all());
    drop(file);
    if let Err(error) = result.and_then(|()| fs::rename(&temporary, path)) {
        fs::remove_file(&temporary)?;
        return Err(error.into());
    }
    Ok(())
}

fn stored_chunks(db: &rusqlite::Connection, upload: &str) -> Result<Vec<(i64, i64, String)>> {
    let mut statement = db.prepare(
        "SELECT chunk_index,cipher_size,cipher_sha256 FROM attachment_chunks WHERE upload_id=? ORDER BY chunk_index",
    )?;
    Ok(statement
        .query_map([upload], |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)))?
        .collect::<rusqlite::Result<Vec<_>>>()?)
}

pub(super) fn hex(bytes: &[u8]) -> String {
    const DIGITS: &[u8; 16] = b"0123456789abcdef";
    let mut result = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        result.push(DIGITS[(byte >> 4) as usize] as char);
        result.push(DIGITS[(byte & 15) as usize] as char);
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lan::{LOBBY, Peer, Room};
    use crate::store::Identity;
    use std::{
        collections::{HashMap, HashSet},
        sync::Mutex,
        time::{Duration, Instant},
    };
    use tokio::sync::oneshot;

    struct Fixture {
        server: Arc<Server>,
        messages: mpsc::Receiver<Message>,
        responses: HashMap<String, Message>,
        owner: Uuid,
        member: Uuid,
        worker: std::thread::JoinHandle<()>,
    }

    impl Fixture {
        fn open(database: &Path) -> Result<Self> {
            let store = Store::open_lan(database)?;
            let (attachments, jobs) = mpsc::channel(64);
            let (history, _) = mpsc::channel(64);
            let (sender, messages) = mpsc::channel(256);
            let owner = Uuid::new_v4();
            let member = Uuid::new_v4();
            let mut clients = HashMap::new();
            let mut codes = HashMap::new();
            for (id, code) in [(owner, "owner"), (member, "member")] {
                let (close, _) = oneshot::channel();
                clients.insert(
                    id,
                    Peer {
                        identity: Identity {
                            username: code.into(),
                            display_code: code.into(),
                            code: code.into(),
                        },
                        room: LOBBY.into(),
                        admin: id == owner,
                        room_roles: true,
                        muted: false,
                        attachment_session: Uuid::new_v4(),
                        send: sender.clone(),
                        close,
                    },
                );
                codes.insert(code.into(), id);
            }
            let rooms = HashMap::from([(
                LOBBY.into(),
                Room {
                    owner: String::new(),
                    private: false,
                    allowed: HashMap::new(),
                    admins: HashSet::new(),
                    password_hash: None,
                    generation: Uuid::new_v4(),
                },
            )]);
            let server = Arc::new(Server {
                admin_code: "owner".into(),
                host_token: None,
                hub: Mutex::new(Hub {
                    store,
                    attachments,
                    history,
                    rooms,
                    clients,
                    codes,
                    last_message: 0,
                    pending: HashMap::new(),
                }),
            });
            let worker = start(&server, database, jobs)?;
            Ok(Self {
                server,
                messages,
                responses: HashMap::new(),
                owner,
                member,
                worker,
            })
        }

        fn send(&self, peer: Uuid, mut request: Message) -> Result<String> {
            request.command_id = Uuid::new_v4().to_string();
            self.server.hub.lock().unwrap().command(peer, &request)?;
            Ok(request.command_id)
        }

        fn response(&mut self, command: &str) -> Result<Message> {
            if let Some(message) = self.responses.remove(command) {
                return Ok(message);
            }
            let deadline = Instant::now() + Duration::from_secs(15);
            while Instant::now() < deadline {
                match self.messages.try_recv() {
                    Ok(message) if message.command_id == command => return Ok(message),
                    Ok(message) => {
                        self.responses.insert(message.command_id.clone(), message);
                    }
                    Err(mpsc::error::TryRecvError::Empty) => (),
                    Err(error) => return Err(error.into()),
                }
                std::thread::sleep(Duration::from_millis(1));
            }
            anyhow::bail!("attachment response timed out for {command}")
        }

        fn request(&mut self, request: Message) -> Result<Message> {
            let id = self.send(self.owner, request)?;
            self.response(&id)
        }

        fn pause(&self) -> (std::sync::mpsc::Receiver<()>, std::sync::mpsc::Sender<()>) {
            let (entered, observer) = std::sync::mpsc::channel();
            let (resume, blocked) = std::sync::mpsc::channel();
            self.server
                .hub
                .lock()
                .unwrap()
                .attachments
                .try_send(Work::Pause(entered, blocked))
                .unwrap();
            (observer, resume)
        }

        fn close(self) {
            drop(self.server);
            self.worker.join().unwrap();
        }
    }

    #[test]
    fn worker_preserves_chat_permissions_and_interrupted_publication() -> Result<()> {
        let root = std::env::temp_dir().join(format!("lan-attachment-worker-{}", Uuid::new_v4()));
        fs::create_dir_all(&root)?;
        let database = root.join("chat.db");
        let mut fixture = Fixture::open(&database)?;
        let (entered, resume) = fixture.pause();
        let pending = fixture.send(
            fixture.owner,
            Message {
                kind: "attachment.init".into(),
                room: LOBBY.into(),
                logical_size: 1,
                ..Message::default()
            },
        )?;
        entered.recv_timeout(Duration::from_secs(5))?;
        let chat_started = Instant::now();
        let chat = fixture.send(
            fixture.member,
            Message {
                kind: "chat".into(),
                content: "chat during blocked attachment".into(),
                message_id: Uuid::new_v4().to_string(),
                ..Message::default()
            },
        )?;
        assert_eq!(
            fixture.response(&chat)?.content,
            "chat during blocked attachment"
        );
        assert!(chat_started.elapsed() < Duration::from_secs(1));
        let mut queued = Vec::new();
        for _ in 0..64 {
            queued.push(fixture.send(
                fixture.owner,
                Message {
                    kind: "attachment.init".into(),
                    room: LOBBY.into(),
                    logical_size: 1,
                    ..Message::default()
                },
            )?);
        }
        assert_eq!(
            fixture
                .send(
                    fixture.owner,
                    Message {
                        kind: "attachment.init".into(),
                        room: LOBBY.into(),
                        logical_size: 1,
                        ..Message::default()
                    }
                )
                .unwrap_err()
                .to_string(),
            "Attachment service is busy"
        );
        fixture
            .server
            .hub
            .lock()
            .unwrap()
            .clients
            .get_mut(&fixture.owner)
            .unwrap()
            .attachment_session = Uuid::new_v4();
        resume.send(())?;
        assert_eq!(fixture.response(&pending)?.kind, "error");
        for command in queued {
            assert_eq!(fixture.response(&command)?.kind, "error");
        }
        let init = fixture.request(Message {
            kind: "attachment.init".into(),
            room: LOBBY.into(),
            logical_size: 1,
            ..Message::default()
        })?;
        assert_eq!(init.kind, "attachment.init");
        let bytes = vec![7; (1 + ATTACHMENT_TAG_BYTES) as usize];
        let mut chunk = Message {
            kind: "attachment.chunk".into(),
            upload_id: init.upload_id.clone(),
            chunk_index: Some(0),
            ciphertext: STANDARD.encode(&bytes),
            cipher_sha256: "incorrect".into(),
            ..Message::default()
        };
        assert_eq!(
            fixture.request(chunk.clone())?.content,
            "Attachment chunk hash mismatch"
        );
        chunk.cipher_sha256 = hex(digest(&SHA256, &bytes).as_ref());
        assert_eq!(fixture.request(chunk)?.content, "stored");
        fixture.server.hub.lock().unwrap().store.0.execute_batch(
            "CREATE TRIGGER fail_attachment_publish BEFORE UPDATE OF committed_at ON attachments
             BEGIN SELECT RAISE(ABORT, 'injected publication failure'); END;",
        )?;
        let commit = Message {
            kind: "attachment.commit".into(),
            upload_id: init.upload_id.clone(),
            ..Message::default()
        };
        let (entered, observer) = std::sync::mpsc::channel();
        let (resume, blocked) = std::sync::mpsc::channel();
        fixture
            .server
            .hub
            .lock()
            .unwrap()
            .attachments
            .try_send(Work::PausePublication(entered, blocked))
            .unwrap();
        let pending_commit = fixture.send(fixture.owner, commit.clone())?;
        observer.recv_timeout(Duration::from_secs(5))?;
        fixture
            .server
            .hub
            .lock()
            .unwrap()
            .clients
            .get_mut(&fixture.owner)
            .unwrap()
            .attachment_session = Uuid::new_v4();
        resume.send(())?;
        assert_eq!(
            fixture.response(&pending_commit)?.content,
            "Attachment access changed; retry from the current channel"
        );
        assert_eq!(fixture.request(commit.clone())?.kind, "error");
        assert!(root.join("attachments").join(&init.attachment_id).is_dir());
        let pending: bool = fixture.server.hub.lock().unwrap().store.0.query_row(
            "SELECT EXISTS(SELECT 1 FROM attachment_uploads u JOIN attachments a ON a.attachment_id=u.attachment_id
             WHERE u.upload_id=? AND a.committed_at IS NULL)", [&init.upload_id], |row| row.get(0))?;
        assert!(pending);
        fixture.close();
        {
            let db = rusqlite::Connection::open(&database)?;
            db.execute_batch("DROP TRIGGER fail_attachment_publish")?;
        }
        let mut fixture = Fixture::open(&database)?;
        let resumed = fixture.request(Message {
            kind: "attachment.resume".into(),
            upload_id: init.upload_id.clone(),
            ..Message::default()
        })?;
        assert_eq!(resumed.received_indexes, vec![0]);
        assert_eq!(fixture.request(commit)?.content, "committed");
        let downloaded = fixture.request(Message {
            kind: "attachment.download".into(),
            attachment_id: init.attachment_id,
            chunk_index: Some(0),
            ..Message::default()
        })?;
        assert_eq!(STANDARD.decode(downloaded.ciphertext)?, bytes);

        let size = 100 * 1024 * 1024;
        let large = fixture.request(Message {
            kind: "attachment.init".into(),
            room: LOBBY.into(),
            logical_size: size,
            ..Message::default()
        })?;
        let partial = root.join("attachments/.partial").join(&large.upload_id);
        fs::create_dir_all(&partial)?;
        {
            let mut hub = fixture.server.hub.lock().unwrap();
            let tx = hub.store.0.transaction()?;
            for index in 0..chunk_count(size) {
                let data = vec![11; expected_cipher_size(size, index) as usize];
                fs::write(chunk_path(&partial, index), &data)?;
                tx.execute("INSERT INTO attachment_chunks(upload_id,chunk_index,cipher_size,cipher_sha256,created_at)
                    VALUES (?,?,?,?,?)", params![large.upload_id,index,data.len() as i64,hex(digest(&SHA256,&data).as_ref()),now()])?;
            }
            tx.commit()?;
        }
        let (entered, resume) = fixture.pause();
        let commit_started = Instant::now();
        let commit_id = fixture.send(
            fixture.owner,
            Message {
                kind: "attachment.commit".into(),
                upload_id: large.upload_id,
                ..Message::default()
            },
        )?;
        entered.recv_timeout(Duration::from_secs(5))?;
        resume.send(())?;
        let chat_started = Instant::now();
        let chat = fixture.send(
            fixture.member,
            Message {
                kind: "chat".into(),
                content: "chat during 100 MiB commit".into(),
                message_id: Uuid::new_v4().to_string(),
                ..Message::default()
            },
        )?;
        assert_eq!(
            fixture.response(&chat)?.content,
            "chat during 100 MiB commit"
        );
        let chat_elapsed = chat_started.elapsed();
        assert_eq!(fixture.response(&commit_id)?.content, "committed");
        eprintln!(
            "100 MiB attachment commit {:?}, concurrent chat {:?}",
            commit_started.elapsed(),
            chat_elapsed
        );
        fixture.close();
        fs::remove_dir_all(root)?;
        Ok(())
    }
}
