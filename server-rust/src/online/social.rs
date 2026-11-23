use std::collections::HashSet;

use anyhow::{Result, bail, ensure};
use lan_chat_protocol::{MAX_FRAME_BYTES, Message, OnlineGroup, OnlineProfile};
use rusqlite::{OptionalExtension, Row, params};
use uuid::Uuid;

use super::Online;
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
        let changed = Message {
            kind: "groups_changed".into(),
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
                reply.groups = vec![group];
                self.deliver(id, reply);
                for member in members {
                    self.deliver_account(&member, &changed);
                }
                return Ok(());
            }
            _ => {}
        }
        let group = self.store.group_for(owner, &request.group_id)?;
        reply.group_id.clone_from(&group.id);
        match request.kind.as_str() {
            "group_detail" => {
                reply.profiles = self.store.group_members(&group.id)?;
                reply.groups = vec![group];
            }
            "group_history" => {
                let mut query = request.clone();
                query.room = group.name;
                query.private = false;
                query.target_user_code.clear();
                return self.history(id, "online_group", &group.id, &query);
            }
            "group_leave" => {
                self.store.0.execute(
                    "DELETE FROM online_group_members WHERE group_id=? AND user_code=?",
                    params![group.id, owner],
                )?;
            }
            "group_send" => {
                return self.send_group(id, identity, &group, request);
            }
            _ => unreachable!(),
        }
        self.deliver(id, reply);
        if request.kind == "group_leave" {
            self.deliver_account(owner, &changed);
        }
        Ok(())
    }

    fn send_group(
        &mut self,
        id: Uuid,
        identity: &Identity,
        group: &OnlineGroup,
        request: &Message,
    ) -> Result<()> {
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
            created_at: now(),
            delivery_state: "sent".into(),
            ..Message::default()
        };
        ensure!(
            serde_json::to_vec(&message)?.len() <= MAX_FRAME_BYTES - 1024,
            Rejected("群消息超过服务器允许的长度")
        );
        let members = self.store.group_members(&group.id)?;
        if self.store.insert_chat(
            &message,
            "online_group",
            &group.id,
            &identity.code,
            "",
            None,
        )? {
            for member in members {
                self.deliver_account(&member.user_code.to_ascii_lowercase(), &message);
            }
        } else {
            (message.username, message.content, message.created_at) = self.store.0.query_row(
                "SELECT sender_username,content,created_at FROM chat_messages
                 WHERE kind='online_group' AND conversation_key=? AND sender_code=? AND message_id=?",
                params![group.id, identity.code, request.message_id],
                |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
            )?;
            self.deliver(id, message);
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
            |row| Ok(OnlineProfile { user_code: row.get(0)?, display_name: row.get(1)?, bio: row.get(2)?, friendship: row.get(3)? }),
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
            "SELECT g.id,g.name,(SELECT COUNT(*) FROM online_group_members WHERE group_id=g.id)
             FROM online_groups g JOIN online_group_members m ON m.group_id=g.id
             WHERE m.user_code=? ORDER BY g.name,g.id",
        )?;
        Ok(statement
            .query_map([owner], group_row)?
            .collect::<rusqlite::Result<Vec<_>>>()?)
    }

    fn group_for(&self, owner: &str, group: &str) -> Result<OnlineGroup> {
        self.0
            .query_row(
                "SELECT g.id,g.name,(SELECT COUNT(*) FROM online_group_members WHERE group_id=g.id)
             FROM online_groups g JOIN online_group_members m ON m.group_id=g.id
             WHERE m.user_code=? AND g.id=?",
                params![owner, group],
                group_row,
            )
            .optional()?
            .ok_or_else(|| Rejected("你无权访问该群聊").into())
    }

    fn group_members(&self, group: &str) -> Result<Vec<OnlineProfile>> {
        let mut statement = self.0.prepare(
            "SELECT a.user_code,COALESCE(p.display_name,a.username)
             FROM online_group_members m JOIN accounts a ON a.normalized_code=m.user_code
             LEFT JOIN online_profiles p ON m.user_code=p.user_code
             WHERE m.group_id=? ORDER BY a.normalized_code",
        )?;
        Ok(statement
            .query_map([group], |row| {
                Ok(OnlineProfile {
                    user_code: row.get(0)?,
                    display_name: row.get(1)?,
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
        };
        let tx = self.0.transaction()?;
        tx.execute(
            "INSERT INTO online_groups(id,name) VALUES (?,?)",
            params![group.id, group.name],
        )?;
        for code in &members {
            let inserted = tx.execute(
                "INSERT INTO online_group_members(group_id,user_code) SELECT ?1,?2
                 WHERE (SELECT COUNT(*) FROM online_group_members WHERE user_code=?2)<100",
                params![group.id, code],
            )?;
            ensure!(inserted == 1, GROUP_MEMBERS);
        }
        tx.commit()?;
        Ok((group, members))
    }
}

fn group_row(row: &Row<'_>) -> rusqlite::Result<OnlineGroup> {
    Ok(OnlineGroup {
        id: row.get(0)?,
        name: row.get(1)?,
        member_count: row.get(2)?,
    })
}

fn valid_text(text: &str, min: usize, max: usize) -> bool {
    (min..=max).contains(&text.chars().count()) && !text.chars().any(char::is_control)
}
