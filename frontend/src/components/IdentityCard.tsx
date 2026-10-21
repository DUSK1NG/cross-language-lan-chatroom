import type { BridgeState } from '../bridge/types';
import { Avatar } from './Avatar';

export function IdentityCard({ state }: { state: BridgeState }) {
  const connected = state.connection.phase === 'connected';
  return (
    <div className="identity-card identity-card--fixed" data-testid="identity-card">
      <span className="identity-card__avatar">
        <Avatar name={state.identity.displayName} seed={state.identity.userCode} size="small" />
        <span className={`identity-status${connected ? '' : ' identity-status--away'}`} aria-label={connected ? 'online' : 'offline'} />
      </span>
      <span className="conversation-copy">
        <strong>{state.identity.displayName} #{state.identity.userCode}</strong>
        <small>{state.identity.admin ? '管理员' : '成员'} · {connected ? '在线' : state.connection.statusText}</small>
      </span>
    </div>
  );
}
