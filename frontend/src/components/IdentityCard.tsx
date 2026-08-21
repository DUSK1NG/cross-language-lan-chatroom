import type { BridgeState } from '../bridge/types';

export function IdentityCard({ state }: { state: BridgeState }) {
  return (
    <div className="identity-card identity-card--fixed" data-testid="identity-card">
      <span className="avatar">{state.identity.displayName.slice(0, 1)}</span>
      <span className="conversation-copy"><strong>{state.identity.displayName} #{state.identity.userCode}</strong></span>
      <span className="identity-status" aria-label="online" />
    </div>
  );
}
