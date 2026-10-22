import { useState } from 'react';
import { Lock } from 'lucide-react';

import type { MemberSummary } from '../../bridge/types';
import { Avatar } from '../ui/avatar';
import { Button } from '../ui/button';
import { Switch } from '../ui/controls';
import { Dialog } from '../ui/dialog';
import { CommandFeedback, type CommandFeedbackProps } from '../CommandFeedback';

export const roomNamePattern = /^[A-Za-z0-9_]{1,32}$/;

const fieldClass = 'h-10 w-full rounded-md border border-control bg-input px-3 text-body text-ink outline-none transition-colors duration-[var(--dur-hover)] placeholder:text-ink-3 focus:border-accent focus-visible:outline-none';

export function CreateRoomDialog({ open, onOpenChange, pending, feedback, onSubmit }: {
  open: boolean; onOpenChange: (open: boolean) => void; pending: boolean; feedback: CommandFeedbackProps;
  onSubmit: (room: string, isPrivate: boolean, reset: () => void) => void;
}) {
  const [name, setName] = useState('');
  const [isPrivate, setIsPrivate] = useState(false);
  const valid = roomNamePattern.test(name.trim());
  const reset = () => { setName(''); setIsPrivate(false); };
  return (
    <Dialog open={open} onOpenChange={(next) => { if (!pending) onOpenChange(next); }} title="新建频道" dismissible={!pending}>
      <form className="flex flex-col gap-4" onSubmit={(event) => { event.preventDefault(); if (valid && !pending) onSubmit(name.trim(), isPrivate, reset); }}>
        <label className="flex flex-col gap-2 text-small text-ink-2">频道名称
          <input aria-label="频道名称" className={fieldClass} value={name} onChange={(event) => setName(event.target.value)} autoFocus placeholder="例如 release_team" />
        </label>
        {name && !valid && <p className="-mt-2 text-small text-warning">只能使用字母、数字和下划线，长度 1-32 位</p>}
        <div className="flex items-center justify-between gap-4 rounded-md bg-bg px-3 py-3">
          <span className="flex items-center gap-2 text-body text-ink"><Lock size={16} aria-hidden="true" className="text-ink-2" />私有频道</span>
          <Switch label="私有频道" checked={isPrivate} onCheckedChange={setIsPrivate} />
        </div>
        <CommandFeedback {...feedback} />
        <div className="flex justify-end gap-2">
          <Button variant="ghost" onClick={() => onOpenChange(false)} disabled={pending}>取消</Button>
          <Button type="submit" variant="primary" aria-label="确认新建频道" disabled={!valid || pending}>创建</Button>
        </div>
      </form>
    </Dialog>
  );
}

export function RoomManageDialog({ open, onOpenChange, room, pending, feedback, onAction }: {
  open: boolean; onOpenChange: (open: boolean) => void; room: string; pending: boolean; feedback: CommandFeedbackProps;
  onAction: (action: 'invite' | 'remove_member' | 'delete', memberCode: string, reset: () => void) => void;
}) {
  const [code, setCode] = useState('');
  const [confirmDelete, setConfirmDelete] = useState(false);
  const reset = () => setCode('');
  return (
    <Dialog open={open} onOpenChange={onOpenChange} title="频道管理" description={`管理 #${room} 的成员与频道。`}>
      <div className="flex flex-col gap-4">
        <label className="flex flex-col gap-2 text-small text-ink-2">成员代码
          <input aria-label="成员代码" className={fieldClass} placeholder="成员代码，例如 B001" value={code} onChange={(event) => setCode(event.target.value)} />
        </label>
        <CommandFeedback {...feedback} />
        <div className="flex flex-wrap justify-end gap-2">
          <Button variant="outline" disabled={!code.trim() || pending} onClick={() => onAction('remove_member', code.trim(), reset)}>移除成员</Button>
          <Button variant="primary" disabled={!code.trim() || pending} onClick={() => onAction('invite', code.trim(), reset)}>邀请成员</Button>
        </div>
        <section className="mt-2 border-t border-divider pt-4">
          <h3 className="text-body font-medium text-ink">删除频道</h3>
          <p className="mt-1 text-small text-ink-2">删除后频道内的消息与附件对所有成员不可见，无法恢复。</p>
          {confirmDelete ? (
            <div className="mt-3 flex justify-end gap-2">
              <Button variant="ghost" onClick={() => setConfirmDelete(false)}>取消</Button>
              <Button variant="danger" disabled={pending} onClick={() => onAction('delete', '', reset)}>确认删除 #{room}</Button>
            </div>
          ) : (
            <div className="mt-3 flex justify-end"><Button variant="danger-outline" disabled={pending} onClick={() => setConfirmDelete(true)}>删除频道</Button></div>
          )}
        </section>
      </div>
    </Dialog>
  );
}

export type AdminAction = 'mute' | 'kick';

export function MemberDialog({ member, localUserCode, canManage, feedback, pending, onOpenChange, onDirect, onAdmin }: {
  member: MemberSummary | null; localUserCode: string; canManage: boolean; feedback: CommandFeedbackProps; pending: boolean;
  onOpenChange: (open: boolean) => void; onDirect: (member: MemberSummary) => void; onAdmin: (action: AdminAction, member: MemberSummary) => void;
}) {
  const isLocal = member?.userCode === localUserCode;
  return (
    <Dialog open={member !== null} onOpenChange={onOpenChange} title="成员资料">
      {member && (
        <div className="flex flex-col items-center gap-3 text-center">
          <Avatar name={member.displayName} seed={member.userCode} size={80} online={member.online} ringClass="ring-elevated" />
          <div>
            <strong className="block text-title font-semibold text-ink">{member.displayName}#{member.userCode}</strong>
            <p className="mt-1 text-small text-ink-2">{member.admin ? '管理员' : '普通成员'} · {member.online ? '在线' : '离线'}</p>
          </div>
          <div className="mt-2 flex w-full flex-wrap justify-center gap-2">
            {!isLocal && <Button variant="primary" onClick={() => onDirect(member)}>私聊</Button>}
            {canManage && !isLocal && !member.admin && <>
              <Button variant="outline" disabled={pending} onClick={() => onAdmin('mute', member)}>禁言/解禁</Button>
              <Button variant="danger-outline" disabled={pending} onClick={() => onAdmin('kick', member)}>踢出</Button>
            </>}
          </div>
          <CommandFeedback {...feedback} />
        </div>
      )}
    </Dialog>
  );
}

export function AdminConfirmDialog({ pendingAction, busy, onCancel, onConfirm }: {
  pendingAction: { action: AdminAction; member: MemberSummary } | null; busy: boolean; onCancel: () => void; onConfirm: () => void;
}) {
  const kick = pendingAction?.action === 'kick';
  return (
    <Dialog open={pendingAction !== null} onOpenChange={(open) => { if (!open) onCancel(); }} title={kick ? '确认踢出成员' : '确认切换禁言'}>
      {pendingAction && <>
        <p className="text-body text-ink"><strong className="font-semibold">{pendingAction.member.displayName}#{pendingAction.member.userCode}</strong></p>
        <p className="mt-1 text-small text-ink-2">{kick ? '确定要将该成员踢出当前频道吗？' : '确定要切换该成员的禁言状态吗？'}</p>
        <div className="mt-6 flex justify-end gap-2">
          <Button variant="ghost" onClick={onCancel}>取消</Button>
          <Button variant={kick ? 'danger' : 'primary'} disabled={busy} onClick={onConfirm}>确认</Button>
        </div>
      </>}
    </Dialog>
  );
}
