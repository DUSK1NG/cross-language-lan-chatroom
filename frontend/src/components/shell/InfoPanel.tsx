import { AnimatePresence, motion } from 'framer-motion';
import { Lock, MessageSquare, Settings2, SlidersHorizontal, X } from 'lucide-react';

import type { BridgeState, MemberSummary } from '../../bridge/types';
import { cn } from '../../lib/cn';
import { ease } from '../../lib/motion';
import { Avatar } from '../ui/avatar';
import { Button } from '../ui/button';
import { IconButton } from '../ui/icon-button';
import { EmptyState } from '../ui/states';

type InfoPanelProps = {
  state: BridgeState;
  open: boolean;
  /** Below 900px the panel overlays the chat with a scrim (spec §2). */
  overlay: boolean;
  onClose: () => void;
  onStartDirect: (member: MemberSummary) => void;
  onViewProfile: (member: MemberSummary) => void;
  canManage: boolean;
  onManageMember: (member: MemberSummary) => void;
  onManageRoom?: () => void;
};

/** Right-hand info panel, 340px (spec §2): the conversation, its members, management. */
export function InfoPanel({ state, open, overlay, onClose, onStartDirect, onViewProfile, canManage, onManageMember, onManageRoom }: InfoPanelProps) {
  const conversation = state.navigation.activeConversation;
  const isRoom = conversation?.kind === 'room';
  const online = state.members.filter((member) => member.online).length;
  const local = state.identity.userCode;
  const sorted = [...state.members].sort((a, b) => Number(b.online) - Number(a.online) || Number(b.admin) - Number(a.admin) || a.displayName.localeCompare(b.displayName));

  return (
    <AnimatePresence initial={false}>
      {open && (
        <>
          {overlay && (
            <motion.div key="scrim" className="absolute inset-0 z-30 bg-overlay" onClick={onClose}
              initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.3, ease }} />
          )}
          <motion.aside key="panel" data-testid="member-panel" data-state="open" aria-label="会话信息"
            initial={{ x: 24, opacity: 0 }} animate={{ x: 0, opacity: 1 }} exit={{ x: 24, opacity: 0 }}
            transition={{ duration: 0.3, ease }}
            className={cn('flex h-full w-[var(--info-width)] shrink-0 flex-col border-l border-divider bg-sidebar',
              overlay && 'absolute right-0 top-0 z-40 shadow-dialog')}>
            <header className="flex h-14 shrink-0 items-center gap-2 px-4">
              <h2 className="flex-1 text-title font-semibold text-ink">{isRoom ? '频道信息' : '用户信息'}</h2>
              <IconButton label="关闭信息面板" onClick={onClose}><X size={20} aria-hidden="true" /></IconButton>
            </header>
            {conversation && (
              <section className="flex flex-col items-center gap-2 border-b border-divider px-4 pb-6 pt-2 text-center">
                <Avatar name={conversation.title} seed={isRoom ? conversation.id : conversation.userCode} size={80} />
                <p className="text-title font-semibold text-ink">{isRoom ? `# ${conversation.title}` : conversation.title}</p>
                <p className="num text-small text-ink-2">
                  {isRoom ? <>{state.members.length} 名成员，<span aria-label="在线成员数量">{online}</span> 人在线</> : `#${conversation.userCode}`}
                </p>
                {conversation.mls && <p className="inline-flex items-center gap-1 text-small text-ink-2"><Lock size={12} aria-hidden="true" />消息与附件端到端加密</p>}
                {isRoom && canManage && onManageRoom && (
                  <Button variant="outline" size="sm" className="mt-2" onClick={onManageRoom}><SlidersHorizontal size={16} aria-hidden="true" />频道管理</Button>
                )}
              </section>
            )}
            <section className="flex min-h-0 flex-1 flex-col">
              <h3 className="px-4 pb-2 pt-4 text-small font-medium text-ink-2">成员</h3>
              {sorted.length === 0 ? <EmptyState icon={<MessageSquare />} title="暂无成员" description="成员上线后会显示在这里。" /> : (
                <ul className="scroll-thin min-h-0 flex-1 overflow-y-auto px-2 pb-2">
                  {sorted.map((member) => {
                    const isLocal = member.userCode === local;
                    return (
                      <li key={member.userCode} className="group flex h-14 items-center gap-3 rounded-md px-2 hover:bg-hover">
                        <button type="button" aria-label={`查看 ${member.displayName}`} onClick={() => onViewProfile(member)} className="press shrink-0 cursor-pointer rounded-full">
                          <Avatar name={member.displayName} seed={member.userCode} size={40} online={member.online} ringClass="ring-sidebar group-hover:ring-hover" />
                        </button>
                        <span className={cn('flex min-w-0 flex-1 flex-col', !member.online && 'opacity-70')}>
                          <span className="flex min-w-0 items-center gap-2">
                            <span className="truncate text-body font-medium text-ink">{member.displayName}</span>
                            {member.admin && <span className="shrink-0 rounded-sm bg-accent-soft px-1 text-small text-accent-text">管理员</span>}
                          </span>
                          <span className="num truncate text-small text-ink-2">#{member.userCode} · {member.online ? '在线' : '离线'}</span>
                        </span>
                        {!isLocal && (
                          <IconButton size="sm" label={`direct-${member.displayName}`} tooltip={`私聊 ${member.displayName}`} onClick={() => onStartDirect(member)}>
                            <MessageSquare size={16} aria-hidden="true" />
                          </IconButton>
                        )}
                        {canManage && !isLocal && (
                          <IconButton size="sm" label={`管理 ${member.displayName}`} onClick={() => onManageMember(member)}>
                            <Settings2 size={16} aria-hidden="true" />
                          </IconButton>
                        )}
                      </li>
                    );
                  })}
                </ul>
              )}
            </section>
          </motion.aside>
        </>
      )}
    </AnimatePresence>
  );
}
