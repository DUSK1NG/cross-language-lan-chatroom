import type { BridgeState, MemberSummary } from '../bridge/types';

type MemberPanelProps = {
  state: BridgeState;
  className?: string;
  localUserCode?: string;
  onStartDirect?: (member: MemberSummary) => void;
  onViewProfile?: (member: MemberSummary) => void;
  canManage?: boolean;
  onManageMember?: (member: MemberSummary) => void;
  onClose?: () => void;
};

export function MemberPanel({ state, className = '', localUserCode, onStartDirect, onViewProfile, canManage = false, onManageMember, onClose }: MemberPanelProps) {
  const onlineCount = state.members.filter((member) => member.online).length;

  return (
    <aside className={`member-panel ${className}`.trim()} data-testid="member-panel" data-motion="workspace-panel" aria-label="成员">
      <div className="sidebar-heading"><div><p className="eyebrow">成员</p><h2>成员</h2></div><span className="member-count" aria-label="在线成员数量">{onlineCount}</span>{onClose && <button type="button" className="icon-button" aria-label="关闭成员列表" onClick={onClose}>×</button>}</div>
      <div className="member-list">
        {state.members.map((member) => (
          <div className="member-row" key={member.userCode}>
            {onViewProfile ? <button type="button" className="member-profile-button" aria-label={`查看 ${member.displayName}`} onClick={() => onViewProfile(member)}><span className="avatar">{member.displayName.slice(0, 1)}</span></button> : <span className="avatar">{member.displayName.slice(0, 1)}</span>}
            <span className="conversation-copy"><strong>{member.displayName}</strong><small>#{member.userCode}</small></span>
            <span className={`presence-dot ${member.online ? 'presence-dot--online' : ''}`} aria-label={member.online ? '在线' : '离线'} />
            {member.admin && <small className="member-admin-badge">管理员</small>}
            {onStartDirect && member.userCode !== localUserCode && (
              <button type="button" className="member-direct-button" aria-label={`direct-${member.displayName}`} onClick={() => onStartDirect(member)}>私聊</button>
            )}
            {canManage && onManageMember && member.userCode !== localUserCode && (
              <button type="button" className="member-direct-button" aria-label={`管理 ${member.displayName}`} onClick={() => onManageMember(member)}>管理</button>
            )}
          </div>
        ))}
      </div>
    </aside>
  );
}
