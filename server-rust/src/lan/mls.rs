use anyhow::{Result, ensure};
use base64::{Engine, engine::general_purpose::STANDARD};
use lan_chat_protocol::{MAX_FRAME_BYTES, Message};
use ring::digest::{SHA256, digest};
use rusqlite::{Connection, OptionalExtension, TransactionBehavior, params};
use uuid::Uuid;

use super::{Hub, Peer, attachments::hex, validate_room_name};
use crate::store::{Rejected, Store, now, user_code};

const MEMBER_REQUIRED: Rejected = Rejected("MLS group member authorization required");
const PENDING: Rejected = Rejected("MLS group has pending welcome");
const EPOCH_ROLLBACK: Rejected = Rejected("MLS group epoch rollback");
const PROPOSAL_MISSING: Rejected = Rejected("MLS group commit requires an accepted proposal");
const PROPOSAL_CONFLICT: Rejected = Rejected("MLS group proposal conflict");
const COMMIT_CONFLICT: Rejected = Rejected("MLS group commit conflict");
const WELCOME_CONFLICT: Rejected = Rejected("MLS group welcome conflict");

impl Hub {
    pub(super) fn mls(&mut self, id: Uuid, command: &Message) -> Result<()> {
        let peer = &self.clients[&id];
        let author = peer.identity.code.clone();
        let mut response = Message {
            kind: command.kind.clone(),
            command_id: command.command_id.clone(),
            ..Message::default()
        };
        match command.kind.as_str() {
            "mls.key_package.publish" => {
                opaque(&command.key_package, "Invalid MLS key package")?;
                self.store.0.execute(
                    "INSERT INTO mls_key_packages (user_code,key_package,updated_at) VALUES (?,?,?)
                     ON CONFLICT(user_code) DO UPDATE SET key_package=excluded.key_package,updated_at=excluded.updated_at",
                    params![author, command.key_package, now()],
                )?;
                response.user_code = peer.identity.display_code.clone();
                response.content = "stored".into();
                self.deliver(id, response);
                return Ok(());
            }
            "mls.key_package.fetch" => {
                let target = user_code(&command.target_user_code)?;
                let room = if command.room.is_empty() && target != author {
                    &peer.room
                } else {
                    &command.room
                };
                ensure!(
                    self.mls_access(peer, &target, room)?,
                    Rejected("MLS key package access denied")
                );
                response.key_package = self
                    .store
                    .0
                    .query_row(
                        "SELECT key_package FROM mls_key_packages WHERE user_code=?",
                        [&target],
                        |row| row.get(0),
                    )
                    .optional()?
                    .ok_or(Rejected("MLS key package not found"))?;
                response.room = room.clone();
                response.target_user_code = target;
                self.deliver(id, response);
                return Ok(());
            }
            _ => (),
        }

        let room = if command.room.is_empty() {
            peer.room.clone()
        } else {
            command.room.clone()
        };
        validate_room_name(&room)?;
        ensure!(
            !command.group_id.is_empty() && command.group_id.len() <= MAX_FRAME_BYTES,
            Rejected("Invalid MLS group ID")
        );
        let epoch = i64::try_from(command.epoch).map_err(|_| Rejected("MLS epoch is too large"))?;
        let control = Control {
            command,
            room: &room,
            epoch,
        };
        response.room = room.clone();
        response.group_id = command.group_id.clone();
        response.epoch = command.epoch;
        response.proposal_id = command.proposal_id.clone();

        match command.kind.as_str() {
            "mls.group.proposal" => {
                let strict = !command.action.is_empty() || !command.target_user_code.is_empty();
                let action = command.action.trim().to_ascii_lowercase();
                let target = if strict {
                    ensure!(
                        matches!(action.as_str(), "add" | "remove"),
                        Rejected("Invalid MLS group proposal")
                    );
                    let target = user_code(&command.target_user_code)?;
                    ensure!(
                        action != "add" || self.mls_access(peer, &target, &room)?,
                        Rejected("MLS group proposal access denied")
                    );
                    target
                } else {
                    ensure!(
                        peer.room == room,
                        Rejected("MLS group proposal access denied")
                    );
                    String::new()
                };
                if let Some(members) =
                    control.propose(&mut self.store, &author, &action, &target)?
                {
                    self.mls_broadcast(
                        id,
                        &room,
                        strict.then_some(members),
                        Message {
                            command_id: String::new(),
                            proposal: command.proposal.clone(),
                            target_user_code: target,
                            content: action,
                            ..response.clone()
                        },
                    );
                    response.content = "stored".into();
                } else {
                    response.content = "duplicate".into();
                }
            }
            "mls.group.commit" => {
                let strict = !command.proposal_id.is_empty();
                ensure!(
                    strict || peer.room == room,
                    Rejected("MLS group commit access denied")
                );
                if let Some(members) = control.commit(&mut self.store, &author)? {
                    self.mls_broadcast(
                        id,
                        &room,
                        strict.then_some(members),
                        Message {
                            command_id: String::new(),
                            commit: command.commit.clone(),
                            ..response.clone()
                        },
                    );
                    response.content = "stored".into();
                } else {
                    response.content = "duplicate".into();
                }
            }
            "mls.group.welcome" => {
                let target = user_code(&command.target_user_code)?;
                let strict = group(&self.store.0, &command.group_id)?.is_some();
                ensure!(
                    self.mls_access(peer, &target, &room)? && (strict || peer.room == room),
                    Rejected("MLS group welcome access denied")
                );
                let digest = if strict {
                    hash(&command.welcome)
                } else {
                    String::new()
                };
                ensure!(
                    !strict
                        || (!command.proposal_id.is_empty()
                            && command.welcome_digest.eq_ignore_ascii_case(&digest)),
                    Rejected("MLS group welcome access denied")
                );
                let inserted =
                    control.welcome(&mut self.store, &author, &target, strict, &digest)?;
                response.target_user_code = target.clone();
                response.welcome_digest = command.welcome_digest.clone();
                if let Some(target) = self.codes.get(&target).copied() {
                    self.deliver(
                        target,
                        Message {
                            command_id: String::new(),
                            welcome: command.welcome.clone(),
                            welcome_digest: digest,
                            ..response.clone()
                        },
                    );
                }
                response.content = if inserted { "stored" } else { "duplicate" }.into();
            }
            "mls.group.welcome.accept" => {
                ensure!(
                    user_code(&command.target_user_code)? == author,
                    Rejected("MLS welcome accept identity denied")
                );
                let accepted = control.accept(&mut self.store, &author)?;
                response.welcome_digest = command.welcome_digest.clone();
                response.content = if accepted { "activated" } else { "duplicate" }.into();
            }
            _ => unreachable!("MLS dispatch"),
        }
        self.deliver(id, response);
        Ok(())
    }

    fn mls_access(&self, peer: &Peer, target: &str, room: &str) -> Result<bool> {
        if peer.identity.code == target {
            return Ok(true);
        }
        let Some(definition) = self.rooms.get(room) else {
            return Ok(false);
        };
        if peer.room != room {
            return Ok(false);
        }
        if definition.allowed.get(target) == Some(&true)
            || self
                .codes
                .get(target)
                .is_some_and(|id| self.clients[id].room == room)
        {
            return Ok(true);
        }
        if definition.private {
            return Ok(false);
        }
        Ok(self.store.0.query_row(
            "SELECT EXISTS(SELECT 1 FROM accounts WHERE normalized_code=?)",
            [target],
            |row| row.get(0),
        )?)
    }

    fn mls_broadcast(
        &mut self,
        sender: Uuid,
        room: &str,
        members: Option<Vec<String>>,
        message: Message,
    ) {
        let peers = if let Some(members) = members {
            members
                .iter()
                .filter_map(|code| self.codes.get(code).copied())
                .collect()
        } else {
            self.room_clients(room)
        };
        for peer in peers {
            if peer != sender {
                self.deliver(peer, message.clone());
            }
        }
    }
}

struct Control<'a> {
    command: &'a Message,
    room: &'a str,
    epoch: i64,
}

impl Control<'_> {
    fn propose(
        &self,
        store: &mut Store,
        author: &str,
        action: &str,
        target: &str,
    ) -> Result<Option<Vec<String>>> {
        let cmd = self.command;
        let strict = !action.is_empty();
        ensure!(
            !cmd.proposal_id.is_empty(),
            Rejected("Invalid MLS proposal ID")
        );
        opaque(&cmd.proposal, "Invalid MLS group proposal")?;
        let digest = hash(&cmd.proposal);
        let tx = store
            .0
            .transaction_with_behavior(TransactionBehavior::Immediate)?;
        let current = open_group(&tx, &cmd.group_id, self.room, PROPOSAL_CONFLICT)?;
        let mut members = members(&tx, &cmd.group_id)?;
        ensure!(strict || members.is_empty(), MEMBER_REQUIRED);
        if strict && members.is_empty() && current < 0 {
            tx.execute(
                "INSERT INTO mls_group_members (group_id,user_code,active,joined_epoch) VALUES (?,?,1,0)
                 ON CONFLICT(group_id,user_code) DO UPDATE SET active=1,removed_epoch=NULL",
                params![cmd.group_id, author],
            )?;
            members.push(author.into());
        }
        ensure!(
            !strict || members.iter().any(|member| member == author),
            MEMBER_REQUIRED
        );
        let existing: Option<(String, String, String)> = tx.query_row(
            "SELECT proposal_data,action,target_code FROM mls_group_proposals WHERE group_id=? AND epoch=? AND proposal_id=?",
            params![cmd.group_id, self.epoch, cmd.proposal_id],
            |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
        ).optional()?;
        if let Some((data, old_action, old_target)) = existing {
            ensure!(
                data == cmd.proposal && (!strict || (old_action == action && old_target == target)),
                PROPOSAL_CONFLICT
            );
            return Ok(None);
        }
        let same_payload: bool = tx.query_row(
            "SELECT EXISTS(SELECT 1 FROM mls_group_proposals WHERE group_id=? AND epoch=? AND proposal_digest=?)",
            params![cmd.group_id, self.epoch, digest], |row| row.get(0),
        )?;
        if same_payload {
            return Ok(None);
        }
        ensure!(self.epoch >= current, EPOCH_ROLLBACK);
        ensure!(!pending(&tx, &cmd.group_id)?, PENDING);
        let target_active = members.iter().any(|member| member == target);
        ensure!(action != "add" || !target_active, PROPOSAL_CONFLICT);
        ensure!(action != "remove" || target_active, MEMBER_REQUIRED);
        tx.execute(
            "INSERT INTO mls_group_proposals
             (group_id,epoch,proposal_id,proposal_data,proposal_digest,action,target_code,member_snapshot,created_at)
             VALUES (?,?,?,?,?,?,?,?,?)",
            params![cmd.group_id, self.epoch, cmd.proposal_id, cmd.proposal, digest,
                if strict { action } else { "add" }, target, serde_json::to_string(&members)?, now()],
        )?;
        tx.commit()?;
        Ok(Some(members))
    }

    fn commit(&self, store: &mut Store, author: &str) -> Result<Option<Vec<String>>> {
        let cmd = self.command;
        opaque(&cmd.commit, "Invalid MLS group commit")?;
        let strict = !cmd.proposal_id.is_empty();
        let tx = store
            .0
            .transaction_with_behavior(TransactionBehavior::Immediate)?;
        let (room, current) = group(&tx, &cmd.group_id)?.ok_or(PROPOSAL_MISSING)?;
        ensure!(
            room == self.room || (!strict && room.is_empty()),
            COMMIT_CONFLICT
        );

        let active = members(&tx, &cmd.group_id)?;
        ensure!(
            if strict {
                active.iter().any(|member| member == author)
            } else {
                active.is_empty()
            },
            MEMBER_REQUIRED
        );
        let existing: Option<(String, String)> = tx
            .query_row(
                "SELECT commit_data,proposal_id FROM mls_group_epochs WHERE group_id=? AND epoch=?",
                params![cmd.group_id, self.epoch],
                |row| Ok((row.get(0)?, row.get(1)?)),
            )
            .optional()?;
        if let Some((data, proposal_id)) = existing {
            ensure!(
                data == cmd.commit && proposal_id == cmd.proposal_id,
                COMMIT_CONFLICT
            );
            return Ok(None);
        }
        ensure!(!pending(&tx, &cmd.group_id)?, PENDING);
        ensure!(self.epoch >= current, EPOCH_ROLLBACK);
        ensure!(
            !strict || current < 0 || current.checked_add(1) == Some(self.epoch),
            COMMIT_CONFLICT
        );
        let (action, target, snapshot): (String, String, String) = tx.query_row(
            "SELECT action,target_code,member_snapshot FROM mls_group_proposals
             WHERE group_id=?1 AND epoch=?2 AND (?3='' OR proposal_id=?3) ORDER BY proposal_id LIMIT 1",
            params![cmd.group_id, self.epoch, cmd.proposal_id],
            |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
        ).optional()?.ok_or(PROPOSAL_MISSING)?;
        let members: Vec<String> = serde_json::from_str(&snapshot)?;
        ensure!(
            !strict || members.iter().all(|member| user_code(member).is_ok()),
            PROPOSAL_CONFLICT
        );
        tx.execute(
            "INSERT INTO mls_group_epochs (group_id,epoch,proposal_id,commit_data,created_at) VALUES (?,?,?,?,?)",
            params![cmd.group_id, self.epoch, cmd.proposal_id, cmd.commit, now()],
        )?;
        tx.execute(
            "UPDATE mls_groups SET current_epoch=?,room=? WHERE group_id=?",
            params![self.epoch, self.room, cmd.group_id],
        )?;
        if strict && action == "remove" {
            tx.execute(
                "UPDATE mls_group_members SET active=0,removed_epoch=? WHERE group_id=? AND user_code=? AND active=1",
                params![self.epoch, cmd.group_id, target],
            )?;
        }
        tx.commit()?;
        Ok(Some(members))
    }

    fn welcome(
        &self,
        store: &mut Store,
        author: &str,
        target: &str,
        strict: bool,
        digest: &str,
    ) -> Result<bool> {
        let cmd = self.command;
        opaque(&cmd.welcome, "Invalid MLS group welcome")?;
        let tx = store
            .0
            .transaction_with_behavior(TransactionBehavior::Immediate)?;
        let current = open_group(&tx, &cmd.group_id, self.room, WELCOME_CONFLICT)?;
        if strict {
            ensure!(current == self.epoch, WELCOME_CONFLICT);
            ensure!(
                members(&tx, &cmd.group_id)?
                    .iter()
                    .any(|member| member == author),
                MEMBER_REQUIRED
            );
            let committed = tx.query_row(
                "SELECT EXISTS(SELECT 1 FROM mls_group_epochs e
                 JOIN mls_group_proposals p ON e.group_id=p.group_id AND e.epoch=p.epoch AND e.proposal_id=p.proposal_id
                 WHERE e.group_id=? AND e.epoch=? AND e.proposal_id=? AND p.action='add' AND p.target_code=?)",
                params![cmd.group_id, self.epoch, cmd.proposal_id, target],
                |row| row.get::<_, bool>(0),
            )?;
            ensure!(
                committed,
                Rejected("MLS group welcome requires a matching add commit")
            );
        }
        let proposal = if strict { &cmd.proposal_id } else { "" };
        let existing: Option<(String, String, String)> = tx.query_row(
            "SELECT welcome,proposal_id,welcome_digest FROM mls_group_welcomes WHERE group_id=? AND epoch=? AND target_code=?",
            params![cmd.group_id, self.epoch, target],
            |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
        ).optional()?;
        if let Some((body, id, sha)) = existing {
            ensure!(
                body == cmd.welcome && id == proposal && sha == digest,
                WELCOME_CONFLICT
            );
            return Ok(false);
        }
        tx.execute(
            "INSERT INTO mls_group_welcomes (group_id,epoch,target_code,welcome,proposal_id,welcome_digest,accepted,created_at)
             VALUES (?,?,?,?,?,?,?,?)",
            params![cmd.group_id, self.epoch, target, cmd.welcome, proposal, digest, !strict, now()],
        )?;
        tx.commit()?;
        Ok(true)
    }

    fn accept(&self, store: &mut Store, target: &str) -> Result<bool> {
        let cmd = self.command;
        ensure!(
            !cmd.proposal_id.is_empty() && cmd.welcome_digest.len() == 64,
            WELCOME_CONFLICT
        );
        let tx = store
            .0
            .transaction_with_behavior(TransactionBehavior::Immediate)?;
        let welcome = params![
            cmd.group_id,
            self.epoch,
            target,
            cmd.proposal_id,
            cmd.welcome_digest
        ];
        let accepted: bool = tx.query_row(
            "SELECT accepted FROM mls_group_welcomes WHERE group_id=? AND epoch=? AND target_code=? AND proposal_id=? AND welcome_digest=?",
            welcome, |row| row.get(0),
        ).optional()?.ok_or(WELCOME_CONFLICT)?;
        if accepted {
            return Ok(false);
        }
        ensure!(
            group(&tx, &cmd.group_id)? == Some((self.room.into(), self.epoch)),
            WELCOME_CONFLICT
        );
        tx.execute("UPDATE mls_group_welcomes SET accepted=1 WHERE group_id=? AND epoch=? AND target_code=?",
            params![cmd.group_id, self.epoch, target])?;
        tx.execute(
            "INSERT INTO mls_group_members (group_id,user_code,active,joined_epoch,removed_epoch) VALUES (?,?,1,?,NULL)
             ON CONFLICT(group_id,user_code) DO UPDATE SET active=1,joined_epoch=excluded.joined_epoch,removed_epoch=NULL",
            params![cmd.group_id, target, self.epoch],
        )?;
        tx.commit()?;
        Ok(true)
    }
}

impl Store {
    pub(super) fn pending_lan_welcomes(&self, target: &str) -> Result<Vec<Message>> {
        let mut statement = self.0.prepare(
            "SELECT w.group_id,g.room,w.epoch,w.target_code,w.proposal_id,w.welcome,w.welcome_digest,g.current_epoch
             FROM mls_group_welcomes w LEFT JOIN mls_groups g ON g.group_id=w.group_id
             WHERE w.target_code=? AND w.accepted=0 ORDER BY w.group_id,w.epoch",
        )?;
        let mut rows = statement.query([target])?;
        let mut messages = Vec::new();
        while let Some(row) = rows.next()? {
            let group_id: String = row.get(0)?;
            let epoch: u64 = row.get(2)?;
            let current: Option<u64> = row.get(7)?;
            ensure!(
                current == Some(epoch),
                "MLS pending welcome is stale: group={group_id}"
            );
            messages.push(Message {
                kind: "mls.group.welcome".into(),
                group_id,
                epoch,
                room: row.get(1)?,
                target_user_code: row.get(3)?,
                proposal_id: row.get(4)?,
                welcome: row.get(5)?,
                welcome_digest: row.get(6)?,
                ..Message::default()
            });
        }
        Ok(messages)
    }
}

fn group(db: &Connection, id: &str) -> Result<Option<(String, i64)>> {
    Ok(db
        .query_row(
            "SELECT room,current_epoch FROM mls_groups WHERE group_id=?",
            [id],
            |row| Ok((row.get(0)?, row.get(1)?)),
        )
        .optional()?)
}

fn open_group(db: &Connection, id: &str, room: &str, conflict: Rejected) -> Result<i64> {
    if let Some((old_room, current)) = group(db, id)? {
        ensure!(old_room == room || old_room.is_empty(), conflict);
        if old_room.is_empty() {
            db.execute(
                "UPDATE mls_groups SET room=? WHERE group_id=?",
                params![room, id],
            )?;
        }
        return Ok(current);
    }
    db.execute(
        "INSERT INTO mls_groups (group_id,room,current_epoch) VALUES (?,?,-1)",
        params![id, room],
    )?;
    Ok(-1)
}

fn members(db: &Connection, id: &str) -> Result<Vec<String>> {
    let mut statement = db.prepare(
        "SELECT user_code FROM mls_group_members WHERE group_id=? AND active=1 ORDER BY user_code",
    )?;
    Ok(statement
        .query_map([id], |row| row.get(0))?
        .collect::<rusqlite::Result<Vec<_>>>()?)
}

fn pending(db: &Connection, id: &str) -> Result<bool> {
    Ok(db.query_row(
        "SELECT EXISTS(SELECT 1 FROM mls_group_welcomes w
         JOIN mls_group_proposals p ON p.group_id=w.group_id AND p.epoch=w.epoch AND p.proposal_id=w.proposal_id
         WHERE w.group_id=? AND w.accepted=0 AND p.action='add')",
        [id], |row| row.get(0),
    )?)
}

fn hash(text: &str) -> String {
    hex(digest(&SHA256, text.as_bytes()).as_ref())
}

fn opaque(value: &str, reason: &'static str) -> Result<()> {
    ensure!(
        !value.is_empty() && value.len() <= MAX_FRAME_BYTES && STANDARD.decode(value).is_ok(),
        Rejected(reason)
    );
    Ok(())
}
