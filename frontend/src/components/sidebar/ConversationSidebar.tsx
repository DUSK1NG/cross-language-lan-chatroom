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

export function ConversationSidebar({ state, bridge, filter, onFilterChange, onCreateRoom, onSettings, onConnectionApprovals, onManageRoom, canManageRoom, onSelected, footer }: ConversationSidebarProps) {
  const [query, setQuery] = useState('');
  const entries = useMemo(() => buildEntries(state, filter, query), [state, filter, query]);
  const total = state.rooms.length + state.directMessages.length;
  const showSearch = total >= searchThreshold || query.length > 0;
  const approvals = state.connectionApprovals?.length ?? 0;

  const select = (entry: ConversationEntry) => {
    bridge.dispatch(entry.kind === 'room'
      ? createCommand('conversation.selectRoom', { room: entry.room.roomName })
      : createCommand('conversation.selectDirect', { userCode: entry.direct.userCode }));
    onSelected?.();
  };

  const menu: MenuItem[] = [
    ...(onSettings ? [{ label: '设置', icon: <Settings />, onSelect: onSettings }] : []),
    ...(state.identity.admin && onConnectionApprovals ? [{ label: '连接审批', icon: <ShieldCheck />, onSelect: onConnectionApprovals, trailing: approvals > 0 ? <span className="num text-small text-danger">{approvals}</span> : undefined }] : []),
    ...(canManageRoom && onManageRoom ? [{ label: '频道管理', icon: <SlidersHorizontal />, onSelect: onManageRoom }] : []),
    { label: '新建频道', icon: <Plus />, onSelect: onCreateRoom },
    { label: '刷新列表', icon: <RefreshCw />, onSelect: () => { bridge.dispatch(createCommand('directory.refreshRooms', {})); bridge.dispatch(createCommand('directory.refreshUsers', {})); } }
  ];

  return (
    <aside data-testid="conversation-sidebar" aria-label="会话列表" className="flex h-full min-w-0 flex-col bg-sidebar">
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
        ) : <span className="flex-1 truncate text-title font-semibold text-ink">LAN Chat</span>}
        <IconButton label="新建频道" onClick={onCreateRoom}><Plus size={20} aria-hidden="true" /></IconButton>
      </div>
      <FilterTabs value={filter} onValueChange={onFilterChange} />
      {entries.length > 0 ? (
        <ConversationList entries={entries} active={state.navigation.activeConversation} onSelect={select}
          label={filter === 'direct' ? '私信列表' : filter === 'rooms' ? '群聊频道列表' : '全部会话'} />
      ) : (
        <div className="min-h-0 flex-1">
          {query ? <EmptyState icon={<Search />} title="没有匹配的会话" description={`没有找到与“${query}”相关的频道或私信。`} />
            : <EmptyState icon={<MessagesSquare />} title={filter === 'direct' ? '还没有私信' : '还没有频道'}
                description={filter === 'direct' ? '在成员列表中选择一位成员即可开始私聊。' : '创建一个频道，或等待房主邀请你加入。'}
                action={<Button variant="primary" onClick={onCreateRoom}><Plus size={16} aria-hidden="true" />创建或加入频道</Button>} />}
        </div>
      )}
      {footer}
    </aside>
  );
}
