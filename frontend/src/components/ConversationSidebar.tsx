import { createCommand } from '../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient } from '../bridge/types';

type ConversationSidebarProps = { state: BridgeState; bridge: ChatBridgeClient };

export function ConversationSidebar({ state, bridge }: ConversationSidebarProps) {
  const active = state.navigation.activeConversation;

  return (
    <aside className="conversation-sidebar" data-testid="conversation-sidebar">
      <div className="sidebar-heading">
        <div>
          <p className="eyebrow">CONVERSATIONS</p>
          <h2>Rooms</h2>
        </div>
        <button className="icon-button" type="button" aria-label="refresh rooms"
          onClick={() => bridge.dispatch(createCommand('directory.refreshRooms', {}))}>↻</button>
      </div>
      <div className="conversation-list" aria-label="Room list">
        {state.rooms.map((room) => (
          <button
            className={`conversation-item ${active?.kind === 'room' && active.id === room.roomName ? 'conversation-item--active' : ''}`}
            key={room.roomName}
            type="button"
            aria-label={`room-${room.roomName}`}
            onClick={() => bridge.dispatch(createCommand('conversation.selectRoom', { room: room.roomName }))}
          >
            <span className="conversation-icon">#</span>
            <span className="conversation-copy"><strong>{room.roomName}</strong><small>{room.memberCount} members</small></span>
            {room.unreadCount > 0 && <span className="unread-badge">{room.unreadCount}</span>}
          </button>
        ))}
      </div>
      <div className="sidebar-heading sidebar-heading--private">
        <div><p className="eyebrow">PRIVATE</p><h2>Messages</h2></div>
        <button className="icon-button" type="button" aria-label="refresh users"
          onClick={() => bridge.dispatch(createCommand('directory.refreshUsers', {}))}>↻</button>
      </div>
      <div className="conversation-list" aria-label="Direct message list">
        {state.directMessages.map((direct) => (
          <button
            className={`conversation-item ${active?.kind === 'dm' && active.userCode === direct.userCode ? 'conversation-item--active' : ''}`}
            key={direct.userCode}
            type="button"
            aria-label={`direct-${direct.displayName.toLowerCase()}`}
            onClick={() => bridge.dispatch(createCommand('conversation.selectDirect', { userCode: direct.userCode }))}
          >
            <span className="avatar avatar--small">{direct.displayName.slice(0, 1)}</span>
            <span className="conversation-copy"><strong>{direct.displayName}</strong><small>#{direct.userCode}</small></span>
            {direct.unreadCount > 0 && <span className="unread-badge">{direct.unreadCount}</span>}
          </button>
        ))}
      </div>
    </aside>
  );
}
