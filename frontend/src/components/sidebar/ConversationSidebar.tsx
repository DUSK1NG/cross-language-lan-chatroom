import { useMemo, useState, type ReactNode } from 'react';
import { Menu, MessagesSquare, Plus, RefreshCw, Search, Settings, ShieldCheck, SlidersHorizontal, X } from 'lucide-react';

import { createCommand } from '../../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient } from '../../bridge/types';
import catBrandMark from '../../assets/lan-chat-cat.png';
import { Button } from '../ui/button';
import { IconButton } from '../ui/icon-button';
import { DropdownMenu, type MenuItem } from '../ui/menu';
import { EmptyState } from '../ui/states';
import { ConversationList } from './ConversationList';
import { FilterTabs } from './FilterTabs';
import { buildEntries, type ConversationEntry, type ConversationFilter } from './conversations';

type ConversationSidebarProps = {
  state: BridgeState;
  bridge: ChatBridgeClient;
  filter: ConversationFilter;
  onFilterChange: (filter: ConversationFilter) => void;
  onCreateRoom: () => void;
  onStartPrivate?: () => void;
  onGroupTools?: () => void;
  onJoinProtectedRoom: (room: string) => void;
  onSettings?: () => void;
  onConnectionApprovals?: () => void;
  onManageRoom?: () => void;
  canManageRoom?: boolean;
  /** Called after a conversation is chosen (the phone layout switches to the chat). */
  onSelected?: () => void;
  footer?: ReactNode;
};

/** Below this many conversations the separate search field is hidden (spec §3). */
const searchThreshold = 6;

export function ConversationSidebar({ state, bridge, filter, onFilterChange, onCreateRoom, onStartPrivate, onGroupTools, onJoinProtectedRoom, onSettings, onConnectionApprovals, onManageRoom, canManageRoom, onSelected, footer }: ConversationSidebarProps) {
  const online = state.edition === 'online';
  const newLabel = online ? '发起私信' : '新建频道';
  const onNew = online ? onStartPrivate : onCreateRoom;
  const [query, setQuery] = useState('');
  const entries = useMemo(() => buildEntries(state, filter, query), [state, filter, query]);
  const total = state.rooms.length + state.directMessages.length + (state.groups?.length ?? 0);
  const showSearch = total >= searchThreshold || query.length > 0;
  const approvals = state.connectionApprovals?.length ?? 0;

  const select = (entry: ConversationEntry) => {
    const active = state.navigation.activeConversation;
    if (entry.kind === 'room' && entry.room.passwordRequired &&
      (active?.kind !== 'room' || active.id !== entry.room.roomName)) {
      onJoinProtectedRoom(entry.room.roomName);
      return;
    }
    bridge.dispatch(entry.kind === 'room'
      ? createCommand('conversation.selectRoom', { room: entry.room.roomName })
      : entry.kind === 'group' ? createCommand('conversation.selectGroup', { groupId: entry.group.id })
      : createCommand('conversation.selectDirect', { userCode: entry.direct.userCode }));
    onSelected?.();
  };

  const menu: MenuItem[] = [
    ...(onSettings ? [{ label: '设置', icon: <Settings />, onSelect: onSettings }] : []),
    ...(state.identity.admin && onConnectionApprovals ? [{ label: '连接审批', icon: <ShieldCheck />, onSelect: onConnectionApprovals, trailing: approvals > 0 ? <span className="num text-small text-danger-text">{approvals}</span> : undefined }] : []),
    ...(canManageRoom && onManageRoom ? [{ label: '频道管理', icon: <SlidersHorizontal />, onSelect: onManageRoom }] : []),
    ...(onNew ? [{ label: newLabel, icon: <Plus />, onSelect: onNew }] : []),
    ...(onGroupTools ? [{ label: '群聊与好友', icon: <MessagesSquare />, onSelect: onGroupTools }] : []),
    { label: '刷新列表', icon: <RefreshCw />, onSelect: () => {
      if (!online) bridge.dispatch(createCommand('directory.refreshRooms', {}));
      if (state.onlineGroups) bridge.dispatch(createCommand('directory.refreshGroups', {}));
      bridge.dispatch(createCommand('directory.refreshUsers', {}));
    } }
  ];

  return (
    <aside data-testid="conversation-sidebar" aria-label="会话列表" className="flex h-full min-w-0 flex-col">
      <div className="flex h-14 shrink-0 items-center gap-2 px-3">
        <DropdownMenu trigger={<IconButton label="打开菜单"><Menu size={20} aria-hidden="true" /></IconButton>} items={menu} />
        <img src={catBrandMark} alt="LAN Chat 猫咪标识" className="size-8 shrink-0 rounded-full" />
        {showSearch ? (
          <label className="group flex h-9 min-w-0 flex-1 items-center gap-2 rounded-full border border-transparent bg-input px-3 transition-ui transition-colors focus-within:border-accent">
            <Search size={16} aria-hidden="true" className="shrink-0 text-ink-3" />
            <input type="search" data-hotkey="search" aria-label="搜索" placeholder="搜索" value={query}
              onChange={(event) => setQuery(event.target.value)}
              className="min-w-0 flex-1 bg-transparent text-body text-ink outline-none placeholder:text-ink-3 [&::-webkit-search-cancel-button]:hidden focus-visible:outline-none" />
            {query && <button type="button" aria-label="清除搜索" onClick={() => setQuery('')} className="press inline-flex cursor-pointer text-ink-3 hover:text-ink"><X size={16} aria-hidden="true" /></button>}
          </label>
        ) : <span className="flex-1 truncate text-title font-semibold text-ink">{online ? 'LAN Chat Online' : 'LAN Chat'}</span>}
        <IconButton label={newLabel} onClick={onNew}><Plus size={20} aria-hidden="true" /></IconButton>
      </div>
      {!online && <FilterTabs value={filter} onValueChange={onFilterChange} />}
      {entries.length > 0 ? (
        <ConversationList entries={entries} active={state.navigation.activeConversation} onSelect={select}
          label={filter === 'direct' ? '私信列表' : filter === 'rooms' ? '群聊频道列表' : '全部会话'} />
      ) : (
        <div className="min-h-0 flex-1">
          {query ? <EmptyState icon={<Search />} title="没有匹配的会话" description={`没有找到与“${query}”相关的频道或私信。`} />
            : online ? <EmptyState icon={<MessagesSquare />} title={onGroupTools ? '还没有会话' : '还没有私信'} description="点击右上角＋，输入对方的用户代码。"
                action={onGroupTools && <Button variant="outline" onClick={onGroupTools}>群聊与好友</Button>} />
            : <EmptyState icon={<MessagesSquare />} title={filter === 'direct' ? '还没有私信' : '还没有频道'}
                description={filter === 'direct' ? '在成员列表中选择一位成员即可开始私聊。' : '创建一个频道，或等待房主邀请你加入。'}
                action={<Button variant="primary" onClick={onCreateRoom}><Plus size={16} aria-hidden="true" />创建或加入频道</Button>} />}
        </div>
      )}
      {footer}
    </aside>
  );
}
