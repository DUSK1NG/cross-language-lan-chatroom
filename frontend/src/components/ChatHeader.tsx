import type { BridgeState } from '../bridge/types';
import { Avatar, avatarTone } from './Avatar';
import { Icon } from './Icon';

type ChatHeaderProps = {
  state: BridgeState;
  onMembers: () => void;
  membersOpen: boolean;
  canManage?: boolean;
  onManageRoom?: () => void;
  onConnectionApprovals?: () => void;
  historyQuery?: string;
  onHistoryQueryChange?: (query: string) => void;
};

export function ChatHeader({ state, onMembers, membersOpen, canManage = false, onManageRoom, onConnectionApprovals, historyQuery = '', onHistoryQueryChange }: ChatHeaderProps) {
  const conversation = state.navigation.activeConversation;
  const onlineMembers = state.members.filter((member) => member.online);
  const onlineCount = onlineMembers.length;
  const stack = (onlineMembers.length > 0 ? onlineMembers : state.members).slice(0, 3);
  const approvalCount = state.connectionApprovals?.length ?? 0;
  const connecting = state.connection.phase === 'connecting' || state.connection.phase === 'reconnecting';
  const reconnectAttempt = state.connection.reconnectAttempt;
  const isRoom = conversation?.kind === 'room';
  const titleSeed = conversation ? (conversation.kind === 'room' ? conversation.id : conversation.userCode) : '';
  return (
    <header className="chat-header">
      {conversation && (isRoom
        ? <span className={`chat-header__glyph avatar--tone-${avatarTone(titleSeed)}`} aria-hidden="true"><Icon name="hash" size={18} /></span>
        : <Avatar name={conversation.title} seed={titleSeed} size="small" />)}
      <div className="chat-header__title">
        <h1 aria-label={isRoom ? `# ${conversation.title}` : undefined}>{isRoom ? conversation.title : conversation?.title ?? '会话'}</h1>
        <p className="header-status">
          <span className={`status-dot${connecting ? ' status-dot--busy' : ''}`} aria-hidden="true" />
          <span className="header-status__label">{state.connection.statusText}{connecting && reconnectAttempt ? ` · 第 ${reconnectAttempt} 次重连` : ''}</span>
          {conversation?.mls && <span className="e2ee-chip" title="该会话通过 MLS 端到端加密"><Icon name="lock" size={11} className="e2ee-chip__icon" /><span className="e2ee-chip__label">端到端加密</span></span>}
        </p>
      </div>
      <div className="chat-header__actions">
        {onHistoryQueryChange && <label className="history-search">
          <Icon name="search" size={14} />
          <input aria-label="搜索当前会话" value={historyQuery} onChange={(event) => onHistoryQueryChange(event.target.value)} placeholder="搜索消息" />
          {historyQuery && <button type="button" aria-label="清除消息搜索" title="清除" onClick={() => onHistoryQueryChange('')}><Icon name="close" size={13} /></button>}
        </label>}
        {canManage && <button className="secondary-button header-action-button" type="button" aria-label="频道管理" onClick={onManageRoom}><Icon name="sliders" size={15} /><span className="header-action-button__label">频道管理</span></button>}
        {state.identity.admin && <button className={`secondary-button header-action-button${approvalCount > 0 ? ' header-action-button--alert' : ''}`} type="button" aria-label="连接审批" onClick={onConnectionApprovals}><Icon name="shield" size={15} /><span className="header-action-button__label">连接审批</span>{approvalCount > 0 && <span className="header-action-button__count">{approvalCount}</span>}</button>}
        <button className={`member-toggle${membersOpen ? ' member-toggle--open' : ''}`} type="button" aria-label="members-toggle" aria-expanded={membersOpen} onClick={onMembers}>
          {stack.length > 0
            ? <span className="avatar-stack">{stack.map((member, index) => <Avatar key={member.userCode} name={member.displayName} seed={member.userCode} size="tiny" className={index > 0 ? 'avatar--offset' : ''} />)}</span>
            : <Icon name="users" size={16} />}
          <span className="member-toggle__count">{onlineCount} 名在线成员</span>
        </button>
      </div>
    </header>
  );
}
