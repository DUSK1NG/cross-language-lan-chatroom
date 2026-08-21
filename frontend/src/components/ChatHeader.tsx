import type { BridgeState } from '../bridge/types';

type ChatHeaderProps = { state: BridgeState; onMembers: () => void };

export function ChatHeader({ state, onMembers }: ChatHeaderProps) {
  const conversation = state.navigation.activeConversation;
  return (
    <header className="chat-header">
      <div>
        <p className="eyebrow">LIVE CONVERSATION</p>
        <h1>{conversation?.kind === 'room' ? `# ${conversation.title}` : conversation?.title ?? 'Conversation'}</h1>
        <p className="header-status"><span className="status-dot" />{state.connection.statusText}</p>
      </div>
      <button className="member-toggle" type="button" aria-label="members-toggle" onClick={onMembers}>
        <span className="avatar-stack"><span className="avatar avatar--tiny">A</span><span className="avatar avatar--tiny avatar--offset">B</span></span>
        <span>{state.members.length} members</span>
      </button>
    </header>
  );
}
