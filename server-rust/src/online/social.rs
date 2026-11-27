use std::collections::HashSet;

use anyhow::{Result, bail, ensure};
use lan_chat_protocol::{MAX_FRAME_BYTES, Message, OnlineGroup, OnlineProfile};
use rusqlite::{OptionalExtension, Row, params};
use uuid::Uuid;

use super::{Online, transfer::PublishedImage};
use crate::store::{Identity, Rejected, Store, now, pair, user_code};

const GROUP_MEMBERS: Rejected = Rejected("群聊成员必须是已确认的好友，且每人最多加入 100 个群");

impl Online {
    pub(super) fn social(
        &mut self,
        id: Uuid,
        identity: &Identity,
        request: &Message,
    ) -> Result<()> {
        let owner = &identity.code;
        let mut reply = Message {
            kind: request.kind.clone(),
            command_id: request.command_id.clone(),
            ..Message::default()
        };
        if request.kind == "friend_list" {
            reply.profiles = self.store.friends(owner)?;
            self.deliver(id, reply);
            return Ok(());
        }
        let target = if request.target_user_code.is_empty() {
            owner.clone()
        } else {
            user_code(&request.target_user_code)?
        };
        let mut profile = self.store.profile(owner, &target)?;
        let (first, second) = pair(owner, &target);
        match request.kind.as_str() {
            "profile_get" => {}
            "profile_save" => {
                let proposed = request
                    .profile
                    .as_ref()
                    .filter(|_| owner == &target)
                    .ok_or(Rejected("只能编辑自己的个人主页"))?;
                let name = proposed.display_name.trim();
                let bio = proposed.bio.trim();
                ensure!(
                    valid_text(name, 1, 32) && valid_text(bio, 0, 120),
                    Rejected("昵称需要 1–32 字，简介最多 120 字，均使用单行文字")
                );
                self.store.0.execute(
                    "INSERT INTO online_profiles(user_code,display_name,bio) VALUES (?,?,?)
                     ON CONFLICT(user_code) DO UPDATE SET display_name=excluded.display_name,bio=excluded.bio",
                    params![owner, name, bio],
                )?;
                profile.display_name = name.into();
                profile.bio = bio.into();
            }
            "friend_request" => {
                ensure!(
                    owner != &target && profile.friendship == "none",
                    Rejected("当前用户已有好友关系或待处理申请")
                );
                let changed = self.store.0.execute(
                    "INSERT INTO online_friendships(first_code,second_code,requested_by)
                     SELECT ?1,?2,?3
                     WHERE (SELECT COUNT(*) FROM online_friendships WHERE first_code=?3 OR second_code=?3)<150
                       AND (SELECT COUNT(*) FROM online_friendships WHERE first_code=?4 OR second_code=?4)<150",
                    params![first, second, owner, target],
                )?;
                ensure!(changed == 1, Rejected("双方之一的好友与申请数量已达上限"));
                profile.friendship = "outgoing".into();
            }
            "friend_respond" => {
                ensure!(
                    profile.friendship == "incoming"
                        && matches!(request.action.as_str(), "accept" | "decline"),
                    Rejected("只能处理发给自己的待确认申请")
                );
                if request.action == "accept" {
                    self.store.0.execute(
                        "UPDATE online_friendships SET accepted=1
                         WHERE first_code=? AND second_code=? AND requested_by=? AND accepted=0",
                        params![first, second, target],
                    )?;
                    profile.friendship = "friend".into();
                } else {
                    self.store.0.execute(
                        "DELETE FROM online_friendships WHERE first_code=? AND second_code=? AND requested_by=? AND accepted=0",
                        params![first, second, target],
                    )?;
                    profile.friendship = "none".into();
                }
            }
            _ => unreachable!(),
        }
        reply.profile = Some(profile);
        self.deliver(id, reply);
        if matches!(request.kind.as_str(), "friend_request" | "friend_respond") {
            let event = Message {
                kind: "friends_changed".into(),
                ..Message::default()
            };
            self.deliver_account(owner, &event);
            self.deliver_account(&target, &event);
        }
        Ok(())
    }

    pub(super) fn group(&mut self, id: Uuid, identity: &Identity, request: &Message) -> Result<()> {
        let owner = &identity.code;
        let mut reply = Message {
            kind: request.kind.clone(),
            command_id: request.command_id.clone(),
            ..Message::default()
        };
        match request.kind.as_str() {
            "group_list" => {
                reply.groups = self.store.groups(owner)?;
                self.deliver(id, reply);
                return Ok(());
            }
            "group_create" => {
                let (group, members) = self.store.create_group(owner, request)?;
                let reply_group_id = group.id.clone();
                reply.groups = vec![group];
                self.deliver(id, reply);
                self.groups_changed(&reply_group_id, members);
                return Ok(());
            }
            _ => {}
        }
        let group = self.store.group_for(owner, &request.group_id)?;
        reply.group_id.clone_from(&group.id);
        let mut changed = Vec::new();
        match request.kind.as_str() {
            "group_detail" => {
                reply.profiles = self.store.group_members(&group.id)?;
                reply.groups = vec![self.store.group_details(group.clone())?];
            }
            "group_history" => {
                let mut query = request.clone();
                query.room = group.name;
                query.private = false;
                query.target_user_code.clear();
                let after = self.store.group_history_start(owner, &group.id)?;
                return self.history(id, "online_group", &group.id, &query, after);
            }
            "group_leave" => {
                ensure!(group.role != "owner", Rejected("请先转让群主再退出群聊"));
                self.store.remove_group_member(&group.id, owner)?;
                self.drop_group_image(&group.id, owner);
                changed.push(owner.clone());
                changed.extend(self.store.group_codes(&group.id)?);
            }
            "group_dissolve" => {
                ensure!(group.role == "owner", Rejected("只有群主可以解散群聊"));
                ensure!(
                    self.peers[&id].group_management,
                    Rejected("客户端未协商群管理能力")
                );
                let mut codes = self.store.dissolve_group(&group.id)?;
                codes.push(owner.clone());
                self.deliver(id, reply);
                self.groups_changed(&group.id, codes);
                return Ok(());
            }
            "group_send" => {
                return self
                    .send_group(id, identity, &group, request, None)
                    .map(|_| ());
            }
            _ => {
                ensure!(
                    self.peers[&id].group_management,
                    Rejected("客户端未协商群管理能力")
                );
                if let Some(target) = self.store.manage_group(owner, &group, request)? {
                    if request.kind == "group_kick" {
                        self.drop_group_image(&group.id, &target);
                    }
                    changed.push(target);
                }
                changed.extend(self.store.group_codes(&group.id)?);
            }
        }
        self.deliver(id, reply);
        self.groups_changed(&group.id, changed);
        Ok(())
    }

    pub(super) fn send_group(
        &mut self,
        id: Uuid,
        identity: &Identity,
        group: &OnlineGroup,
        request: &Message,
        image: Option<&PublishedImage>,
    ) -> Result<bool> {
        let parsed =
            Uuid::parse_str(&request.message_id).map_err(|_| Rejected("群消息正文或编号无效"))?;
        ensure!(
            parsed.to_string() == request.message_id && !request.content.trim().is_empty(),
            Rejected("群消息正文或编号无效")
        );
        let profile = self.store.profile(&identity.code, &identity.code)?;
        let mut message = Message {
            kind: "group_message".into(),
            group_id: group.id.clone(),
            room: group.name.clone(),
            message_id: request.message_id.clone(),
            user_code: identity.display_code.clone(),
            username: profile.display_name,
            content: request.content.clone(),
            image_size: image.map_or(0, |image| image.size),
            created_at: now(),
            delivery_state: "sent".into(),
            ..Message::default()
        };
        ensure!(
            serde_json::to_vec(&message)?.len() <= MAX_FRAME_BYTES - 1024,
            Rejected("群消息超过服务器允许的长度")
        );
        match self
            .store
            .insert_online_message(super::transfer::OnlineMessage {
                message: &message,
                kind: "online_group",
                key: &group.id,
                author: &identity.code,
                target: "",
                image,
                undelivered: super::transfer::Quota { limit: 0, bytes: 0 },
            })? {
            super::transfer::InsertOutcome::Inserted => {
                self.publish_group(&identity.code, &message)?;
                Ok(true)
            }
            super::transfer::InsertOutcome::Duplicate { .. } => {
                (message.username, message.content, message.created_at, message.image_size) = self.store.0.query_row(
                    "SELECT sender_username,content,created_at,COALESCE(online_image_size,0) FROM chat_messages
                     WHERE kind='online_group' AND conversation_key=? AND sender_code=? AND message_id=?",
                    params![group.id, identity.code, request.message_id],
                    |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?, row.get(3)?)),
                )?;
                self.deliver(id, message);
                Ok(false)
            }
        }
    }

    fn groups_changed(&mut self, group: &str, mut members: Vec<String>) {
        members.sort_unstable();
        members.dedup();
        let notice = Message {
            kind: "groups_changed".into(),
            group_id: group.into(),
            ..Message::default()
        };
        for code in members {
            self.deliver_account(&code, &notice);
        }
    }

    fn drop_group_image(&mut self, group: &str, user: &str) {
        for peer in self
            .peers
            .values_mut()
            .filter(|peer| peer.identity.code == user)
        {
            if peer
                .image
                .as_ref()
                .is_some_and(|upload| upload.group_id == group)
            {
                peer.image = None;
            }
        }
    }

    pub(super) fn publish_group(&mut self, sender: &str, message: &Message) -> Result<()> {
        let members = self.store.group_codes(&message.group_id)?;
        let mut legacy = message.clone();
        super::hide_group_attachment(&mut legacy);
        self.peers.retain(|id, peer| {
            if members.binary_search(&peer.identity.code).is_err() {
                return true;
            }
            let payload = if peer.group_attachments {
                message
            } else {
                &legacy
            };
            match peer.sender.try_send(payload.clone()) {
                Ok(()) => true,
                Err(error) => {
                    eprintln!("client {id}: {error}");
                    false
                }
            }
        });
        if let Some(push) = &self.push {
            for code in members.iter().filter(|code| code.as_str() != sender) {
                push.notify_group(code, sender, &message.message_id, &message.group_id);
            }
        }
        Ok(())
    }
}

impl Store {
    fn profile(&self, owner: &str, target: &str) -> Result<OnlineProfile> {
        let (first, second) = pair(owner, target);
        self.0.query_row(
            "SELECT a.user_code,COALESCE(p.display_name,a.username),COALESCE(p.bio,''),
                CASE WHEN a.normalized_code=?1 THEN 'self' WHEN f.accepted=1 THEN 'friend'
                     WHEN f.requested_by=?1 THEN 'outgoing' WHEN f.requested_by IS NOT NULL THEN 'incoming' ELSE 'none' END
             FROM accounts a LEFT JOIN online_profiles p ON p.user_code=a.normalized_code
             LEFT JOIN online_friendships f ON f.first_code=?2 AND f.second_code=?3
             WHERE a.normalized_code=?4 AND a.password_hash<>''",
            params![owner, first, second, target],
            |row| Ok(OnlineProfile { user_code: row.get(0)?, display_name: row.get(1)?, bio: row.get(2)?, friendship: row.get(3)?, ..OnlineProfile::default() }),
        ).optional()?.ok_or_else(|| Rejected("找不到该用户代码").into())
    }

    fn friends(&self, owner: &str) -> Result<Vec<OnlineProfile>> {
        let mut statement = self.0.prepare(
            "SELECT a.user_code,COALESCE(p.display_name,a.username),
                CASE WHEN f.accepted=1 THEN 'friend' WHEN f.requested_by=?1 THEN 'outgoing' ELSE 'incoming' END
             FROM online_friendships f JOIN accounts a ON a.normalized_code=
                CASE WHEN f.first_code=?1 THEN f.second_code ELSE f.first_code END
             LEFT JOIN online_profiles p ON p.user_code=a.normalized_code
             WHERE f.first_code=?1 OR f.second_code=?1 ORDER BY f.accepted,a.normalized_code",
        )?;
        Ok(statement
            .query_map([owner], |row| {
                Ok(OnlineProfile {
                    user_code: row.get(0)?,
                    display_name: row.get(1)?,
                    friendship: row.get(2)?,
                    ..OnlineProfile::default()
                })
            })?
            .collect::<rusqlite::Result<Vec<_>>>()?)
    }

    fn groups(&self, owner: &str) -> Result<Vec<OnlineGroup>> {
        let mut statement = self.0.prepare(
            "SELECT g.id,g.name,(SELECT COUNT(*) FROM online_group_members WHERE group_id=g.id),COALESCE(g.owner_code,''),m.role,g.created_at,m.invited_by,m.joined_after
             FROM online_groups g JOIN online_group_members m ON m.group_id=g.id
             WHERE m.user_code=? ORDER BY g.name,g.id",
        )?;
        Ok(statement
            .query_map([owner], group_row)?
            .collect::<rusqlite::Result<Vec<_>>>()?)
    }

    pub(super) fn group_for(&self, owner: &str, group: &str) -> Result<OnlineGroup> {
        self.0
            .query_row(
                "SELECT g.id,g.name,(SELECT COUNT(*) FROM online_group_members WHERE group_id=g.id),COALESCE(g.owner_code,''),m.role,g.created_at,m.invited_by,m.joined_after
             FROM online_groups g JOIN online_group_members m ON m.group_id=g.id
             WHERE m.user_code=? AND g.id=?",
                params![owner, group],
                group_row,
            )
            .optional()?
            .ok_or_else(|| Rejected("你无权访问该群聊").into())
    }

    fn group_details(&self, mut group: OnlineGroup) -> Result<OnlineGroup> {
        group.announcement = self.0.query_row(
            "SELECT announcement FROM online_groups WHERE id=?",
            [&group.id],
            |row| row.get(0),
        )?;
        Ok(group)
    }

    pub(super) fn group_history_start(&self, owner: &str, group: &str) -> Result<i64> {
        self.0
            .query_row(
                "SELECT joined_after FROM online_group_members WHERE group_id=? AND user_code=?",
                params![group, owner],
                |row| row.get(0),
            )
            .optional()?
            .ok_or_else(|| Rejected("你无权访问该群聊").into())
    }

    pub(super) fn group_codes(&self, group: &str) -> Result<Vec<String>> {
        let mut query = self.0.prepare(
            "SELECT user_code FROM online_group_members WHERE group_id=? ORDER BY user_code",
        )?;
        Ok(query
            .query_map([group], |row| row.get(0))?
            .collect::<rusqlite::Result<Vec<_>>>()?)
    }

    fn group_members(&self, group: &str) -> Result<Vec<OnlineProfile>> {
        let mut statement = self.0.prepare(
            "SELECT a.user_code,COALESCE(p.display_name,a.username),m.role,m.joined_at
             FROM online_group_members m JOIN accounts a ON a.normalized_code=m.user_code
             LEFT JOIN online_profiles p ON m.user_code=p.user_code
             WHERE m.group_id=? ORDER BY a.normalized_code",
        )?;
        Ok(statement
            .query_map([group], |row| {
                Ok(OnlineProfile {
                    user_code: row.get(0)?,
                    display_name: row.get(1)?,
                    role: row.get(2)?,
                    joined_at: row.get(3)?,
                    ..OnlineProfile::default()
                })
            })?
            .collect::<rusqlite::Result<Vec<_>>>()?)
    }

    fn create_group(
        &mut self,
        owner: &str,
        request: &Message,
    ) -> Result<(OnlineGroup, Vec<String>)> {
        let name = request.room.trim();
        ensure!(
            valid_text(name, 1, 32) && (1..=19).contains(&request.users.len()),
            Rejected("群名需要 1–32 字，请选择 1–19 位好友")
        );
        let mut seen = HashSet::from([owner.to_owned()]);
        let mut members = vec![owner.to_owned()];
        for invited in &request.users {
            let code = user_code(invited).map_err(|_| GROUP_MEMBERS)?;
            ensure!(seen.insert(code.clone()), GROUP_MEMBERS);
            match self.profile(owner, &code) {
                Ok(profile) if profile.friendship == "friend" => members.push(code),
                Ok(_) => bail!(GROUP_MEMBERS),
                Err(error) if error.is::<Rejected>() => bail!(GROUP_MEMBERS),
                Err(error) => return Err(error),
            }
        }
        let group = OnlineGroup {
            id: Uuid::new_v4().to_string(),
            name: name.into(),
            member_count: members.len() as i64,
            owner_user_code: owner.into(),
            role: "owner".into(),
            created_at: now(),
            ..OnlineGroup::default()
        };
        let tx = self.0.transaction()?;
        tx.execute(
            "INSERT INTO online_groups(id,name,owner_code,created_at) VALUES (?,?,?,?)",
            params![group.id, group.name, owner, group.created_at],
        )?;
        for code in &members {
            let inserted = tx.execute(
                "INSERT INTO online_group_members(group_id,user_code,role,invited_by,joined_at) SELECT ?1,?2,?3,?4,?5
                 WHERE (SELECT COUNT(*) FROM online_group_members WHERE user_code=?2)<100",
                params![group.id, code, if code == owner { "owner" } else { "member" }, if code == owner { "" } else { owner }, group.created_at],
            )?;
            ensure!(inserted == 1, GROUP_MEMBERS);
        }
        tx.commit()?;
        Ok((group, members))
    }

    fn manage_group(
        &mut self,
        actor: &str,
        group: &OnlineGroup,
        request: &Message,
    ) -> Result<Option<String>> {
        let denied = Rejected("你的群角色无权执行此操作");
        let missing = Rejected("目标用户不是该群的成员");
        ensure!(
            !group.owner_user_code.is_empty(),
            Rejected("旧群归属待确认")
        );
        ensure!(matches!(group.role.as_str(), "owner" | "admin"), denied);
        let owner = group.role == "owner";
        let id = &group.id;
        match request.kind.as_str() {
            "group_update" | "group_announce" => {
                let value = request.content.trim();
                let sql = if request.kind == "group_update" {
                    ensure!(owner, denied);
                    ensure!(valid_text(value, 1, 32), Rejected("群名需要 1–32 字"));
                    "UPDATE online_groups SET name=? WHERE id=?"
                } else {
                    ensure!(
                        valid_text(value, 0, 300),
                        Rejected("群公告最多 300 个单行字符")
                    );
                    "UPDATE online_groups SET announcement=? WHERE id=?"
                };
                self.0.execute(sql, params![value, id])?;
                return Ok(None);
            }
            _ => {}
        }
        let target = user_code(&request.target_user_code)?;
        ensure!(target != actor, denied);
        if request.kind == "group_invite" {
            ensure!(
                group.member_count < 20 && self.profile(actor, &target)?.friendship == "friend",
                GROUP_MEMBERS
            );
            let inserted = self.0.execute(
                "INSERT INTO online_group_members(group_id,user_code,invited_by,joined_after,joined_at)
                 SELECT ?1,?2,?3,COALESCE((SELECT MAX(id) FROM chat_messages WHERE kind='online_group' AND conversation_key=?1),0),?4
                 WHERE (SELECT COUNT(*) FROM online_group_members WHERE user_code=?2)<100
                 ON CONFLICT DO NOTHING",
                params![id, target, actor, now()],
            )?;
            ensure!(inserted == 1, Rejected("目标已在群内或已达到加群上限"));
            return Ok(Some(target));
        }
        let role: String = self
            .0
            .query_row(
                "SELECT role FROM online_group_members WHERE group_id=? AND user_code=?",
                params![id, target],
                |row| row.get(0),
            )
            .optional()?
            .ok_or(missing)?;
        match request.kind.as_str() {
            "group_kick" => {
                ensure!(role != "owner" && (owner || role == "member"), denied);
                self.remove_group_member(id, &target)?;
            }
            "group_role" => {
                ensure!(
                    owner && matches!(request.content.as_str(), "admin" | "member"),
                    denied
                );
                self.0.execute(
                    "UPDATE online_group_members SET role=? WHERE group_id=? AND user_code=?",
                    params![request.content, id, target],
                )?;
            }
            "group_transfer" => {
                ensure!(owner, denied);
                let tx = self.0.transaction()?;
                tx.execute(
                    "UPDATE online_groups SET owner_code=? WHERE id=?",
                    params![target, id],
                )?;
                tx.execute("UPDATE online_group_members SET role=CASE user_code WHEN ?1 THEN 'owner' ELSE 'member' END WHERE group_id=?2 AND user_code IN (?1,?3)", params![target, id, actor])?;
                tx.commit()?;
            }
            _ => unreachable!("group management dispatch"),
        }
        Ok(Some(target))
    }

    fn remove_group_member(&mut self, group: &str, member: &str) -> Result<()> {
        let tx = self.0.transaction()?;
        tx.execute(
            "DELETE FROM online_group_members WHERE group_id=? AND user_code=?",
            params![group, member],
        )?;
        tx.execute(
            "DELETE FROM online_files WHERE group_id=? AND owner=? AND chat_id IS NULL",
            params![group, member],
        )?;
        tx.commit()?;
        Ok(())
    }

    fn dissolve_group(&mut self, group: &str) -> Result<Vec<String>> {
        let members = {
            let mut query = self.0.prepare(
                "SELECT user_code FROM online_group_members WHERE group_id=? ORDER BY user_code",
            )?;
            query
                .query_map([group], |row| row.get(0))?
                .collect::<rusqlite::Result<Vec<String>>>()?
        };
        let tx = self.0.transaction()?;
        tx.execute(
            "DELETE FROM chat_messages WHERE kind='online_group' AND conversation_key=?",
            [group],
        )?;
        tx.execute(
            "DELETE FROM online_file_chunks WHERE file_id IN (SELECT id FROM online_files WHERE group_id=?)",
            [group],
        )?;
        tx.execute("DELETE FROM online_files WHERE group_id=?", [group])?;
        tx.execute("DELETE FROM online_group_members WHERE group_id=?", [group])?;
        tx.execute("DELETE FROM online_groups WHERE id=?", [group])?;
        tx.commit()?;
        Ok(members)
    }
}

fn group_row(row: &Row<'_>) -> rusqlite::Result<OnlineGroup> {
    Ok(OnlineGroup {
        id: row.get(0)?,
        name: row.get(1)?,
        member_count: row.get(2)?,
        owner_user_code: row.get(3)?,
        role: row.get(4)?,
        created_at: row.get(5)?,
        invited_by_user_code: row.get(6)?,
        joined_after: row.get(7)?,
        ..OnlineGroup::default()
    })
}

fn valid_text(text: &str, min: usize, max: usize) -> bool {
    (min..=max).contains(&text.chars().count()) && !text.chars().any(char::is_control)
}
