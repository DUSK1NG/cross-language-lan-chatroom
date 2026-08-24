import type { BridgeState } from '../bridge/types';

type ChatHeaderProps = {
  state: BridgeState;
  onMembers: () => void;
  canManage?: boolean;
  onManageRoom?: () => void;
  onConnectionApprovals?: () => void;
};

export function ChatHeader({ state, onMembers, canManage = false, onManageRoom, onConnectionApprovals }: ChatHeaderProps) {
  const conversation = state.navigation.activeConversation;
  const onlineCount = state.members.filter((member) => member.online).length;
  const approvalCount = state.connectionApprovals?.length ?? 0;
  return (
    <header className="chat-header">
      <div>
        <p className="eyebrow">当前会话</p>
        <h1>{conversation?.kind === 'room' ? `# ${conversation.title}` : conversation?.title ?? '会话'}</h1>
        <p className="header-status"><span className="status-dot" />{state.connection.statusText}</p>
      </div>
      <div className="chat-header__actions">
        {canManage && <button className="secondary-button header-action-button" type="button" aria-label="频道管理" onClick={onManageRoom}>频道管理</button>}
        {state.identity.admin && <button className="secondary-button header-action-button" type="button" aria-label="连接审批" onClick={onConnectionApprovals}>连接审批{approvalCount > 0 ? ` (${approvalCount})` : ''}</button>}
        <button className="member-toggle" type="button" aria-label="members-toggle" onClick={onMembers}>
          <span className="avatar-stack"><span className="avatar avatar--tiny">A</span><span className="avatar avatar--tiny avatar--offset">B</span></span>
          <span>{onlineCount} 名在线成员</span>
        </button>
      </div>
    </header>
  );
}
