type WorkspaceRailProps = { onSettings?: () => void };

export function WorkspaceRail({ onSettings }: WorkspaceRailProps) {
  return (
    <nav className="workspace-rail" data-testid="workspace-rail" aria-label="Workspace navigation">
      <div className="brand-mark" aria-label="LAN Chat">LC</div>
      <div className="rail-actions">
        <button className="rail-button rail-button--active" type="button" aria-label="rooms">#</button>
        <button className="rail-button" type="button" aria-label="direct messages">@</button>
      </div>
      <button className="rail-button rail-button--bottom" type="button" aria-label="settings" onClick={onSettings}>⚙</button>
    </nav>
  );
}
