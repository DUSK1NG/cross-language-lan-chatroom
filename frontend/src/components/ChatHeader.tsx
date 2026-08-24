import type { BridgeState } from '../bridge/types';

type ChatHeaderProps = {
  state: BridgeState;
  onMembers: () => void;
  canManage?: boolean;
  onManageRoom?: () => void;
  onDeviceApprovals?: () => void;
};

export function ChatHeader({ state, onMembers, canManage = false, onManageRoom, onDeviceApprovals }: ChatHeaderProps) {
  const conversation = state.navigation.activeConversation;
  const onlineCount = state.members.filter((member) => member.online).length;
  const approvalCount = state.deviceApprovals?.length ?? 0;
  return (
    <header className="chat-header">
      <div>
        <p className="eyebrow">当前会话</p>
        <h1>{conversation?.kind === 'room' ? `# ${conversation.title}` : conversation?.title ?? '会话'}</h1>
        <p className="header-status"><span className="status-dot" />{state.connection.statusText}</p>
      </div>
      <div className="chat-header__actions">
        {canManage && <button className="secondary-button header-action-button" type="button" aria-label="频道管理" onClick={onManageRoom}>频道管理</button>}
        {state.identity.admin && approvalCount > 0 && <button className="secondary-button header-action-button" type="button" aria-label="设备审批" onClick={onDeviceApprovals}>设备审批 ({approvalCount})</button>}
        <button className="member-toggle" type="button" aria-label="members-toggle" onClick={onMembers}>
          <span className="avatar-stack"><span className="avatar avatar--tiny">A</span><span className="avatar avatar--tiny avatar--offset">B</span></span>
          <span>{onlineCount} 名在线成员</span>
        </button>
      </div>
    </header>
  );
}
