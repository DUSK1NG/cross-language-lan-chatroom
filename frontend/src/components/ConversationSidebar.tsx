import { createCommand } from '../bridge/chatBridge';
import { useLayoutEffect, useRef, useState } from 'react';
import { animateStagger } from '../animation/motion';
import type { BridgeState, ChatBridgeClient } from '../bridge/types';
import catBrandMark from '../assets/lan-chat-cat.png';
import { Avatar, avatarTone } from './Avatar';
import { Icon } from './Icon';

export type WorkspaceSection = 'rooms' | 'direct';

type ConversationSidebarProps = {
  state: BridgeState;
  bridge: ChatBridgeClient;
  section: WorkspaceSection;
  onSectionChange?: (section: WorkspaceSection) => void;
  onCreateRoom(): void;
  onSettings?: () => void;
  onOpenSidebar?: () => void;
};

export function ConversationSidebar({ state, bridge, section, onSectionChange, onCreateRoom, onSettings, onOpenSidebar }: ConversationSidebarProps) {
  const active = state.navigation.activeConversation;
  const showRooms = section === 'rooms';
  const [query, setQuery] = useState('');
  const normalizedQuery = query.trim().toLowerCase();
  const rooms = state.rooms.filter((room) => room.roomName.toLowerCase().includes(normalizedQuery));
  const directMessages = state.directMessages.filter((direct) => `${direct.displayName} ${direct.userCode}`.toLowerCase().includes(normalizedQuery));
  const listRef = useRef<HTMLDivElement>(null);
  // Re-run the list entrance whenever the user flips between 群 / 私.
  useLayoutEffect(() => animateStagger(listRef.current, '.conversation-item', { distance: 8 }), [section]);

  return (
    <aside className="conversation-sidebar" data-testid="conversation-sidebar">
      <div className="sidebar-top">
        <img className="brand-mark" src={catBrandMark} alt="LAN Chat 猫咪标识" />
        <div className="section-seg" aria-label="会话分区" data-active={showRooms ? 'rooms' : 'direct'}>
          <button type="button" aria-label="群" title="群聊" aria-pressed={showRooms} onClick={() => onSectionChange?.('rooms')}>群</button>
          <button type="button" aria-label="私" title="私信" aria-pressed={!showRooms} onClick={() => onSectionChange?.('direct')}>私</button>
        </div>
        {onOpenSidebar && <button className="icon-button rail-button--sidebar" type="button" aria-label="打开导航" title="打开导航" onClick={onOpenSidebar}><Icon name="menu" /></button>}
        {onSettings && <button className="icon-button" type="button" aria-label="设置" title="设置" onClick={onSettings}><Icon name="settings" /></button>}
      </div>
      <div className="sidebar-heading">
        <h2>{showRooms ? '频道' : '私信'}<span className="sidebar-heading__count">{showRooms ? state.rooms.length : state.directMessages.length}</span></h2>
        <div className="sidebar-actions">
          {showRooms ? <button className="icon-button" type="button" aria-label="新建频道" title="新建频道" onClick={onCreateRoom}><Icon name="plus" /></button> : null}
          <button className="icon-button" type="button" aria-label={showRooms ? '刷新频道' : '刷新私信'} title={showRooms ? '刷新频道' : '刷新私信'}
            onClick={() => bridge.dispatch(createCommand(showRooms ? 'directory.refreshRooms' : 'directory.refreshUsers', {}))}><Icon name="refresh" size={16} /></button>
        </div>
      </div>
      <label className="conversation-search-field">
        <Icon name="search" size={15} />
        <input className="conversation-search" type="search" aria-label={showRooms ? '搜索群聊' : '搜索私信'} placeholder={showRooms ? '搜索频道…' : '搜索私信…'} value={query} onChange={(event) => setQuery(event.target.value)} />
      </label>
      {showRooms ? <div ref={listRef} className="conversation-list" aria-label="群聊频道列表">
        {rooms.map((room) => (
          <button className={`conversation-item ${active?.kind === 'room' && active.id === room.roomName ? 'conversation-item--active' : ''}`} key={room.roomName} type="button" aria-label={`room-${room.roomName}`} aria-current={active?.kind === 'room' && active.id === room.roomName ? 'page' : undefined} onClick={() => bridge.dispatch(createCommand('conversation.selectRoom', { room: room.roomName }))}>
            <span className={`conversation-icon avatar--tone-${avatarTone(room.roomName)}`} aria-hidden="true"><Icon name={room.private ? 'lock' : 'hash'} size={16} /></span>
            <span className="conversation-copy"><strong>{room.roomName}{room.mls && <span className="conversation-lock" title="端到端加密" aria-label={`${room.roomName} 端到端加密`}><Icon name="shield" size={13} /></span>}</strong><small>{room.memberCount} 人{room.private ? ' · 私有' : ''}</small></span>
            {room.unreadCount > 0 && <span key={room.unreadCount} className="unread-badge">{room.unreadCount > 99 ? '99+' : room.unreadCount}</span>}
          </button>
        ))}
        {rooms.length === 0 && <p className="conversation-empty">{normalizedQuery ? '没有匹配的频道' : '暂无频道'}</p>}
      </div> : <div ref={listRef} className="conversation-list" aria-label="私信列表">
        {directMessages.map((direct) => (
          <button className={`conversation-item ${active?.kind === 'dm' && active.userCode === direct.userCode ? 'conversation-item--active' : ''}`} key={direct.userCode} type="button" aria-label={`direct-${direct.displayName.toLowerCase()}`} aria-current={active?.kind === 'dm' && active.userCode === direct.userCode ? 'page' : undefined} onClick={() => bridge.dispatch(createCommand('conversation.selectDirect', { userCode: direct.userCode }))}>
            <Avatar name={direct.displayName} seed={direct.userCode} size="small" />
            <span className="conversation-copy"><strong>{direct.displayName}</strong><small>#{direct.userCode}</small></span>
            {direct.unreadCount > 0 && <span key={direct.unreadCount} className="unread-badge">{direct.unreadCount > 99 ? '99+' : direct.unreadCount}</span>}
          </button>
        ))}
        {directMessages.length === 0 && <p className="conversation-empty">{normalizedQuery ? '没有匹配的私信' : '暂无私信'}</p>}
      </div>}
    </aside>
  );
}
