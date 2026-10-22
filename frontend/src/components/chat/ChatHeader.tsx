import { useEffect, useRef, useState } from 'react';
import { motion, useAnimationControls } from 'framer-motion';
import { ArrowLeft, Lock, MoreVertical, RefreshCw, Search, ShieldCheck, SlidersHorizontal, Users, X } from 'lucide-react';

import type { BridgeState } from '../../bridge/types';
import { Avatar } from '../ui/avatar';
import { IconButton } from '../ui/icon-button';
import { DropdownMenu, type MenuItem } from '../ui/menu';
import { cn } from '../../lib/cn';

type ChatHeaderProps = {
  state: BridgeState;
  infoOpen: boolean;
  onToggleInfo: () => void;
  canManage?: boolean;
  onManageRoom?: () => void;
  onConnectionApprovals?: () => void;
  onRefresh?: () => void;
  historyQuery?: string;
  onHistoryQueryChange?: (query: string) => void;
  searchOpen: boolean;
  onSearchOpenChange: (open: boolean) => void;
  /** Phone layout: back to the conversation list. */
  onBack?: () => void;
};

/** 56px header (spec §4): avatar · name · subtitle, then search / approvals / more. */
export function ChatHeader({ state, infoOpen, onToggleInfo, canManage = false, onManageRoom, onConnectionApprovals, onRefresh, historyQuery = '', onHistoryQueryChange, searchOpen, onSearchOpenChange, onBack }: ChatHeaderProps) {
  const conversation = state.navigation.activeConversation;
  const isRoom = conversation?.kind === 'room';
  const online = state.members.filter((member) => member.online).length;
  const peer = conversation?.kind === 'dm' ? state.members.find((member) => member.userCode === conversation.userCode) : undefined;
  const subtitle = isRoom
    ? `${state.members.length} 名成员，${online} 人在线`
    : peer?.online ? '在线' : '离线';
  const approvals = state.connectionApprovals?.length ?? 0;
  const searchRef = useRef<HTMLInputElement>(null);

  useEffect(() => { if (searchOpen) searchRef.current?.focus(); }, [searchOpen]);

  const more: MenuItem[] = [
    { label: infoOpen ? '隐藏成员列表' : '成员列表', icon: <Users />, onSelect: onToggleInfo },
    ...(canManage && isRoom && onManageRoom ? [{ label: '频道管理', icon: <SlidersHorizontal />, onSelect: onManageRoom }] : []),
    ...(onRefresh ? [{ label: '刷新', icon: <RefreshCw />, onSelect: onRefresh }] : [])
  ];

  return (
    <header className="flex h-14 shrink-0 items-center gap-2 border-b border-divider bg-sidebar px-2">
      {onBack && <IconButton label="返回会话列表" onClick={onBack}><ArrowLeft size={20} aria-hidden="true" /></IconButton>}
      {conversation && (
        <button type="button" aria-label="会话信息" aria-expanded={infoOpen} onClick={onToggleInfo}
          className="press flex min-w-0 flex-1 cursor-pointer items-center gap-3 rounded-md px-2 py-1 text-left hover:bg-hover">
          <Avatar name={conversation.title} seed={isRoom ? conversation.id : conversation.userCode} size={40}
            online={conversation.kind === 'dm' ? peer?.online === true : undefined} />
          <span className="flex min-w-0 flex-col">
            <h1 aria-label={isRoom ? `# ${conversation.title}` : undefined} className="truncate text-name font-semibold text-ink">{conversation.title}</h1>
            <span className="flex min-w-0 items-center gap-2 text-small text-ink-2">
              <span className="num truncate">{subtitle}</span>
              {conversation.mls && (
                <span className="inline-flex shrink-0 items-center gap-1 text-ink-2" title="该会话通过 MLS 端到端加密">
                  <Lock size={12} aria-hidden="true" data-icon="e2ee" />端到端加密
                </span>
              )}
            </span>
          </span>
        </button>
      )}
      {!conversation && <span className="flex-1" />}
      {searchOpen && onHistoryQueryChange && (
        <label className="flex h-9 w-[240px] min-w-0 items-center gap-2 rounded-full border border-accent bg-input px-3">
          <Search size={16} aria-hidden="true" className="shrink-0 text-ink-3" />
          <input ref={searchRef} aria-label="搜索当前会话" placeholder="搜索消息" value={historyQuery}
            onChange={(event) => onHistoryQueryChange(event.target.value)}
            onKeyDown={(event) => { if (event.key === 'Escape') { event.stopPropagation(); onHistoryQueryChange(''); onSearchOpenChange(false); } }}
            className="min-w-0 flex-1 bg-transparent text-body text-ink outline-none placeholder:text-ink-3 focus-visible:outline-none" />
          {historyQuery && (
            <button type="button" aria-label="清除消息搜索" onClick={() => onHistoryQueryChange('')} className="press inline-flex cursor-pointer text-ink-3 hover:text-ink">
              <X size={16} aria-hidden="true" />
            </button>
          )}
        </label>
      )}
      {onHistoryQueryChange && (
        <IconButton label={searchOpen ? '关闭消息搜索' : '搜索消息'} tooltip={searchOpen ? '关闭搜索' : '搜索消息（Ctrl+F）'}
          onClick={() => { if (searchOpen) onHistoryQueryChange(''); onSearchOpenChange(!searchOpen); }}>
          {searchOpen ? <X size={20} aria-hidden="true" /> : <Search size={20} aria-hidden="true" />}
        </IconButton>
      )}
      {state.identity.admin && onConnectionApprovals && (
        <IconButton label="连接审批" tooltip={approvals > 0 ? `连接审批（${approvals} 条待处理）` : '连接审批'} onClick={onConnectionApprovals}
          badge={<ApprovalBadge count={approvals} />}>
          <ShieldCheck size={20} aria-hidden="true" />
        </IconButton>
      )}
      <DropdownMenu align="end" items={more}
        trigger={<IconButton label="更多"><MoreVertical size={20} aria-hidden="true" /></IconButton>} />
    </header>
  );
}

/** Red count badge that bounces once when the count goes up (spec §4). */
function ApprovalBadge({ count }: { count: number }) {
  const controls = useAnimationControls();
  const [last, setLast] = useState(count);
  useEffect(() => {
    if (count > last) void controls.start({ scale: [1, 1.35, 0.92, 1], transition: { duration: 0.45, ease: [0.2, 0.8, 0.2, 1] } });
    setLast(count);
  }, [count, last, controls]);
  if (count <= 0) return null;
  return (
    <motion.span animate={controls} aria-hidden="true"
      className={cn('num absolute -right-1 -top-1 inline-flex h-5 min-w-5 items-center justify-center rounded-full bg-danger-strong px-1 text-small font-medium leading-none text-on-accent ring-2 ring-sidebar')}>
      {count > 99 ? '99+' : count}
    </motion.span>
  );
}
