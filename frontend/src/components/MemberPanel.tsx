import type { BridgeState } from '../bridge/types';

type MemberPanelProps = { state: BridgeState; className?: string };

export function MemberPanel({ state, className = '' }: MemberPanelProps) {
  return (
    <aside className={`member-panel ${className}`.trim()} data-testid="member-panel" aria-label="Members">
      <div className="sidebar-heading"><div><p className="eyebrow">PEOPLE</p><h2>Members</h2></div><span className="member-count">{state.members.length}</span></div>
      <div className="member-list">
        {state.members.map((member) => (
          <div className="member-row" key={member.userCode}>
            <span className="avatar">{member.displayName.slice(0, 1)}</span>
            <span className="conversation-copy"><strong>{member.displayName}</strong><small>#{member.userCode}</small></span>
            <span className={`presence-dot ${member.online ? 'presence-dot--online' : ''}`} aria-label={member.online ? 'online' : 'offline'} />
          </div>
        ))}
      </div>
    </aside>
  );
}
