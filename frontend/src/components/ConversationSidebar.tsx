import { createCommand } from '../bridge/chatBridge';
import { useState } from 'react';
import type { BridgeState, ChatBridgeClient } from '../bridge/types';
import type { WorkspaceSection } from './WorkspaceRail';

type ConversationSidebarProps = { state: BridgeState; bridge: ChatBridgeClient; section: WorkspaceSection; onCreateRoom(): void };

export function ConversationSidebar({ state, bridge, section, onCreateRoom }: ConversationSidebarProps) {
  const active = state.navigation.activeConversation;
  const showRooms = section === 'rooms';
  const [query, setQuery] = useState('');
  const normalizedQuery = query.trim().toLowerCase();
  const rooms = state.rooms.filter((room) => room.roomName.toLowerCase().includes(normalizedQuery));
  const directMessages = state.directMessages.filter((direct) => `${direct.displayName} ${direct.userCode}`.toLowerCase().includes(normalizedQuery));

  return (
    <aside className="conversation-sidebar" data-testid="conversation-sidebar" data-motion="workspace-panel">
      <div className="sidebar-heading">
        <div>
          <p className="eyebrow">{showRooms ? '群聊' : '私信'}</p>
          <h2>{showRooms ? '频道' : '消息'}</h2>
        </div>
        <div className="sidebar-actions">
          {showRooms ? <button className="icon-button" type="button" aria-label="新建频道" title="新建频道" onClick={onCreateRoom}>+</button> : null}
          <button className="icon-button" type="button" aria-label={showRooms ? '刷新频道' : '刷新私信'} title={showRooms ? '刷新频道' : '刷新私信'}
            onClick={() => bridge.dispatch(createCommand(showRooms ? 'directory.refreshRooms' : 'directory.refreshUsers', {}))}>↻</button>
        </div>
      </div>
      <input className="conversation-search" type="search" aria-label={showRooms ? '搜索群聊' : '搜索私信'} placeholder={showRooms ? '搜索频道…' : '搜索私信…'} value={query} onChange={(event) => setQuery(event.target.value)} />
      {showRooms ? <div className="conversation-list" aria-label="群聊频道列表">
        {rooms.map((room) => (
          <button className={`conversation-item ${active?.kind === 'room' && active.id === room.roomName ? 'conversation-item--active' : ''}`} key={room.roomName} type="button" aria-label={`room-${room.roomName}`} onClick={() => bridge.dispatch(createCommand('conversation.selectRoom', { room: room.roomName }))}>
            <span className="conversation-icon">#</span>
            <span className="conversation-copy"><strong>{room.roomName}</strong><small>{room.memberCount} 人</small></span>
            {room.unreadCount > 0 && <span className="unread-badge">{room.unreadCount}</span>}
          </button>
        ))}
      </div> : <div className="conversation-list" aria-label="私信列表">
        {directMessages.map((direct) => (
          <button className={`conversation-item ${active?.kind === 'dm' && active.userCode === direct.userCode ? 'conversation-item--active' : ''}`} key={direct.userCode} type="button" aria-label={`direct-${direct.displayName.toLowerCase()}`} onClick={() => bridge.dispatch(createCommand('conversation.selectDirect', { userCode: direct.userCode }))}>
            <span className="avatar avatar--small">{direct.displayName.slice(0, 1)}</span>
            <span className="conversation-copy"><strong>{direct.displayName}</strong><small>#{direct.userCode}</small></span>
            {direct.unreadCount > 0 && <span className="unread-badge">{direct.unreadCount}</span>}
          </button>
        ))}
      </div>}
    </aside>
  );
}
