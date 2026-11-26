import { AnimatePresence, motion } from 'framer-motion';
import type { ReactNode } from 'react';
import { Lock, MessageSquare, Settings2, SlidersHorizontal, X } from 'lucide-react';

import type { BridgeState, MemberSummary } from '../../bridge/types';
import { cn } from '../../lib/cn';
import { duration, ease, easeExit, scaled } from '../../lib/motion';
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
  groupContent?: ReactNode;
};

/** Right-hand info panel, 340px (spec §2): the conversation, its members, management. */
export function InfoPanel({ state, open, overlay, onClose, onStartDirect, onViewProfile, canManage, onManageMember, onManageRoom, groupContent }: InfoPanelProps) {
  const conversation = state.navigation.activeConversation;
  const isRoom = conversation?.kind === 'room';
  const isGroup = conversation?.kind === 'group';
  const online = state.members.filter((member) => member.online).length;
  const local = state.identity.userCode;
  const sorted = [...state.members].sort((a, b) => Number(b.online) - Number(a.online) || Number(b.admin) - Number(a.admin) || a.displayName.localeCompare(b.displayName));

  return (
    <AnimatePresence initial={false}>
      {open && (
        <>
          {overlay && (
            <motion.div key="scrim" className="absolute inset-0 z-30 bg-overlay" onClick={onClose}
              initial={{ opacity: 0 }} animate={{ opacity: 1 }}
              exit={{ opacity: 0, transition: { duration: scaled(duration.exit), ease: easeExit } }}
              transition={{ duration: scaled(duration.surface), ease }} />
          )}
          <motion.aside key="panel" data-testid="member-panel" data-state="open" aria-label="会话信息"
            initial={{ x: 20, opacity: 0 }} animate={{ x: 0, opacity: 1 }}
            exit={{ x: 20, opacity: 0, transition: { duration: scaled(duration.exit), ease: easeExit } }}
            transition={{ duration: scaled(duration.open), ease, opacity: { duration: scaled(duration.surface), ease } }}
            className={cn('flex h-full w-[var(--info-width)] shrink-0 flex-col border-l border-divider',
              overlay ? 'surface-glass absolute right-0 top-0 z-40 border-l border-divider shadow-dialog' : 'bg-sidebar')}>
            <header className="flex h-[52px] shrink-0 items-center gap-2 px-3">
              <h2 className="flex-1 text-title font-semibold text-ink">{isGroup ? '群聊信息' : isRoom ? '频道信息' : '用户信息'}</h2>
              <IconButton label="关闭信息面板" onClick={onClose}><X size={20} aria-hidden="true" /></IconButton>
            </header>
            {isGroup ? groupContent : conversation && (
              <section className="flex flex-col items-center gap-2 border-b border-divider px-4 pb-6 pt-2 text-center">
                <Avatar name={conversation.title} seed={conversation.kind === 'dm' ? conversation.userCode : conversation.id} size={44} />
                <p className="text-body font-medium text-ink">{isRoom ? `# ${conversation.title}` : conversation.title}</p>
                <p className="num text-small text-ink-2">
                  {isRoom ? <>{state.members.length} 名成员，<span aria-label="在线成员数量">{online}</span> 人在线</> : conversation.kind === 'dm' && `#${conversation.userCode}`}
                </p>
                {conversation.mls && <p className="inline-flex items-center gap-1 text-small text-ink-2"><Lock size={12} aria-hidden="true" />消息与附件端到端加密</p>}
                {isRoom && canManage && onManageRoom && (
                  <Button variant="outline" size="sm" className="mt-2" onClick={onManageRoom}><SlidersHorizontal size={16} aria-hidden="true" />频道管理</Button>
                )}
              </section>
            )}
            {state.edition !== 'online' && <section className="flex min-h-0 flex-1 flex-col">
              <h3 className="px-3 pb-2 pt-4 text-small font-medium text-ink-3">成员</h3>
              {sorted.length === 0 ? <EmptyState icon={<MessageSquare />} title="暂无成员" description="成员上线后会显示在这里。" /> : (
                <ul className="scroll-thin min-h-0 flex-1 overflow-y-auto px-2 pb-2">
                  {sorted.map((member) => {
                    const isLocal = member.userCode === local;
                    return (
                      <li key={member.userCode} className="group flex h-12 items-center gap-2.5 rounded-md px-2 hover:bg-hover">
                        <button type="button" aria-label={`查看 ${member.displayName}`} onClick={() => onViewProfile(member)} className="press shrink-0 cursor-pointer rounded-full">
                          <Avatar name={member.displayName} seed={member.userCode} size={32} online={member.online} ringClass="ring-sidebar group-hover:ring-hover" />
                        </button>
                        <span className={cn('flex min-w-0 flex-1 flex-col', !member.online && 'opacity-70')}>
                          <span className="flex min-w-0 items-center gap-2">
                            <span className="truncate text-body font-medium text-ink">{member.displayName}</span>
                            {member.admin && <span className="shrink-0 rounded-sm bg-hover px-1 text-small text-ink-2">管理员</span>}
                          </span>
                          <span className="num truncate text-small text-ink-3">#{member.userCode} · {member.online ? '在线' : '离线'}</span>
                        </span>
                        {!isLocal && (
                          <IconButton size="sm" label={`direct-${member.displayName}`} tooltip={`私聊 ${member.displayName}`} onClick={() => onStartDirect(member)}>
                            <MessageSquare size={16} aria-hidden="true" />
                          </IconButton>
                        )}
                        {state.identity.admin && !isLocal && (
                          <IconButton size="sm" label={`管理 ${member.displayName}`} onClick={() => onManageMember(member)}>
                            <Settings2 size={16} aria-hidden="true" />
                          </IconButton>
                        )}
                      </li>
                    );
                  })}
                </ul>
              )}
            </section>}
          </motion.aside>
        </>
      )}
    </AnimatePresence>
  );
}
