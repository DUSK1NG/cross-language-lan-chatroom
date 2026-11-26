import { useEffect, useRef, useState } from 'react';

import { createCommand } from '../../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient, GroupSummary } from '../../bridge/types';
import { CommandFeedback, type CommandFeedbackProps } from '../CommandFeedback';
import { Button } from '../ui/button';
import { Dialog } from '../ui/dialog';

const field = 'min-w-0 rounded-md border border-control bg-input px-3 py-2 text-body text-ink';
const label = 'flex flex-col gap-1 text-small text-ink-2';

function useOnlineAction(bridge: ChatBridgeClient) {
  const [feedback, setFeedback] = useState<CommandFeedbackProps>({ status: 'idle', message: '' });
  const stop = useRef<(() => void) | null>(null);
  useEffect(() => () => { stop.current?.(); }, []);

  function send(type: string, payload: Record<string, unknown>, done?: () => void) {
    if (stop.current) return;
    const command = createCommand(type, payload);
    setFeedback({ status: 'pending', message: '正在处理…' });
    stop.current = bridge.subscribeCommandResult((result) => {
      if (result.id !== command.id) return;
      stop.current?.();
      stop.current = null;
      setFeedback(result.ok ? { status: 'success', message: '已完成' }
        : { status: 'error', message: result.error?.message ?? '操作失败' });
      if (result.ok) done?.();
    });
    bridge.dispatch(command);
  }

  return { send, feedback, pending: feedback.status === 'pending' };
}

export function OnlineGroupTools({ bridge, state, onClose, onCreated }: {
  bridge: ChatBridgeClient; state: BridgeState; onClose: () => void; onCreated: () => void;
}) {
  const { send, feedback, pending } = useOnlineAction(bridge);
  const [name, setName] = useState('');
  const [code, setCode] = useState('');
  const [members, setMembers] = useState<string[]>([]);
  const friends = state.friends ?? [];
  const selected = members.filter((member) => friends.some(({ userCode, friendship }) => userCode === member && friendship === 'friend'));

  return (
    <Dialog open title="群聊与好友" dismissible={!pending} onOpenChange={(open) => { if (!open) onClose(); }}>
      <div className="flex flex-col gap-5">
        <form className="flex flex-col gap-2" onSubmit={(event) => {
          event.preventDefault();
          if (name.trim() && selected.length) send('group.create', { room: name.trim(), users: selected }, onCreated);
        }}>
          <label className={label}>群名称<input className={field} value={name} maxLength={32} required autoFocus onChange={(event) => setName(event.target.value)} /></label>
          <fieldset className="flex max-h-44 flex-col gap-2 overflow-auto">
            <legend className="mb-2 text-small text-ink-2">邀请好友（最多 19 位）</legend>
            {friends.filter(({ friendship }) => friendship === 'friend').map(({ userCode, displayName }) => (
              <label key={userCode} className="flex items-center gap-2 text-body">
                <input type="checkbox" checked={members.includes(userCode)} disabled={pending || (!members.includes(userCode) && selected.length === 19)}
                  onChange={(event) => setMembers(event.target.checked ? [...members, userCode] : members.filter((member) => member !== userCode))} />
                {displayName} · #{userCode}
              </label>
            ))}
          </fieldset>
          <Button type="submit" variant="primary" disabled={pending || !name.trim() || selected.length === 0}>创建群聊</Button>
        </form>
        <form className="flex items-end gap-2 border-t border-divider pt-4" onSubmit={(event) => {
          event.preventDefault();
          if (code.trim()) send('friend.request', { targetUserCode: code.trim() }, () => setCode(''));
        }}>
          <label className={`${label} flex-1`}>好友用户代码<input className={field} value={code} required maxLength={16} onChange={(event) => setCode(event.target.value)} /></label>
          <Button type="submit" disabled={pending || !code.trim()}>添加好友</Button>
        </form>
        {friends.filter(({ friendship }) => friendship === 'incoming' || friendship === 'outgoing').map(({ userCode, displayName, friendship }) => (
          <div key={userCode} className="flex flex-wrap items-center gap-2 text-body">
            <span className="min-w-0 flex-1 truncate">{displayName} · #{userCode}</span>
            {friendship === 'incoming' ? <>
              <Button size="sm" disabled={pending} onClick={() => send('friend.respond', { targetUserCode: userCode, action: 'accept' })}>接受</Button>
              <Button size="sm" variant="ghost" disabled={pending} onClick={() => send('friend.respond', { targetUserCode: userCode, action: 'decline' })}>拒绝</Button>
            </> : <span className="text-small text-ink-2">等待确认</span>}
          </div>
        ))}
        <CommandFeedback {...feedback} />
      </div>
    </Dialog>
  );
}

type GroupChange = { action: 'leave' | 'kick' | 'transfer' | 'dissolve'; title: string; userCode?: string };

export function OnlineGroupInfo({ bridge, state, group }: {
  bridge: ChatBridgeClient; state: BridgeState; group: GroupSummary;
}) {
  const { send, feedback, pending } = useOnlineAction(bridge);
  const [confirm, setConfirm] = useState<GroupChange | null>(null);
  const canManage = state.groupManagement && (group.role === 'owner' || group.role === 'admin');
  const owner = state.groupManagement && group.role === 'owner';
  const members = state.groupMembers ?? [];
  const available = (state.friends ?? []).filter(({ userCode, friendship }) => friendship === 'friend'
    && !members.some((member) => member.userCode.toLowerCase() === userCode.toLowerCase()));
  const change = (action: string, fields: Record<string, unknown> = {}, done?: () => void) =>
    send('group.action', { groupId: group.id, action, ...fields }, done);

  return (
    <div className="scroll-thin flex min-h-0 flex-1 flex-col gap-4 overflow-y-auto px-4 pb-5">
      <div>
        <p className="break-words text-title font-semibold">{group.name}</p>
        {group.invitedByUserCode && <p className="text-small text-ink-2">由 #{group.invitedByUserCode} 邀请</p>}
        {state.groupManagement && !group.ownerUserCode && <p className="text-small text-ink-2">群主归属待确认</p>}
        {group.announcement && !canManage && <p className="mt-2 whitespace-pre-wrap break-words text-body">{group.announcement}</p>}
      </div>
      {canManage && <section className="flex flex-col gap-3 border-y border-divider py-3">
        {owner && <form key={`name:${group.name}`} className="flex items-end gap-2" onSubmit={(event) => {
          event.preventDefault(); change('rename', { content: String(new FormData(event.currentTarget).get('name') ?? '').trim() });
        }}>
          <label className={`${label} flex-1`}>群名称<input className={field} name="name" defaultValue={group.name} maxLength={32} required /></label>
          <Button size="sm" type="submit" disabled={pending}>保存</Button>
        </form>}
        <form key={`announcement:${group.announcement}`} className="flex items-end gap-2" onSubmit={(event) => {
          event.preventDefault(); change('announce', { content: String(new FormData(event.currentTarget).get('announcement') ?? '') });
        }}>
          <label className={`${label} flex-1`}>群公告<input className={field} name="announcement" maxLength={300} defaultValue={group.announcement} /></label>
          <Button size="sm" type="submit" disabled={pending}>发布</Button>
        </form>
        {available.length > 0 && group.memberCount < 20 && <form className="flex items-end gap-2" onSubmit={(event) => {
          event.preventDefault(); change('invite', { targetUserCode: new FormData(event.currentTarget).get('friend') });
        }}>
          <label className={`${label} flex-1`}>邀请好友<select name="friend" className={field} required defaultValue="">
            <option value="" disabled>选择好友</option>
            {available.map(({ userCode, displayName }) => <option value={userCode} key={userCode}>{displayName}</option>)}
          </select></label>
          <Button size="sm" type="submit" disabled={pending}>邀请</Button>
        </form>}
      </section>}
      <section aria-label="群成员">
        <h3 className="mb-2 text-small text-ink-2">成员 · {group.memberCount}</h3>
        <ul className="flex flex-col gap-2">
          {members.map(({ userCode, displayName, role }) => {
            const canEdit = canManage && role !== 'owner' && userCode.toLowerCase() !== state.identity.userCode.toLowerCase();
            return <li key={userCode} className="flex flex-col gap-1 rounded-md bg-input px-2 py-2">
              <p className="truncate text-body">{displayName} · #{userCode}{role === 'owner' ? ' · 群主' : role === 'admin' ? ' · 管理员' : ''}</p>
              {canEdit && <div className="flex flex-wrap gap-1">
                {owner && <>
                  <Button size="sm" variant="ghost" disabled={pending} onClick={() => change('role', { targetUserCode: userCode, content: role === 'admin' ? 'member' : 'admin' })}>{role === 'admin' ? '撤销管理员' : '设为管理员'}</Button>
                  <Button size="sm" variant="ghost" disabled={pending} onClick={() => setConfirm({ action: 'transfer', title: `将群主转让给 ${displayName}`, userCode })}>转让群主</Button>
                </>}
                {(owner || role === 'member') && <Button size="sm" variant="danger-outline" disabled={pending}
                  onClick={() => setConfirm({ action: 'kick', title: `移除群成员 ${displayName}`, userCode })}>移除</Button>}
              </div>}
            </li>;
          })}
        </ul>
      </section>
      {owner ? <div className="flex flex-col gap-1">
        <p className="text-small text-ink-2">解散后所有成员都会被移出，聊天记录会被删除。</p>
        <Button variant="danger-outline" disabled={pending} onClick={() => setConfirm({ action: 'dissolve', title: `解散 ${group.name}` })}>解散群聊</Button>
      </div> : <Button variant="danger-outline" disabled={pending} onClick={() => setConfirm({ action: 'leave', title: `退出 ${group.name}` })}>退出群聊</Button>}
      {!confirm && <CommandFeedback {...feedback} />}
      {confirm && <Dialog open title={confirm.title} dismissible={!pending} onOpenChange={(open) => { if (!open) setConfirm(null); }}>
        <CommandFeedback {...feedback} />
        <div className="flex justify-end gap-2">
          <Button variant="ghost" autoFocus disabled={pending} onClick={() => setConfirm(null)}>取消</Button>
          <Button variant="danger" disabled={pending} onClick={() => change(confirm.action, confirm.userCode ? { targetUserCode: confirm.userCode } : {}, () => setConfirm(null))}>确认</Button>
        </div>
      </Dialog>}
    </div>
  );
}
