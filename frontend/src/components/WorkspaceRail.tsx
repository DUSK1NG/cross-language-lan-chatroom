import catBrandMark from '../assets/lan-chat-cat.png';

export type WorkspaceSection = 'rooms' | 'direct';

type WorkspaceRailProps = {
  section: WorkspaceSection;
  onSectionChange: (section: WorkspaceSection) => void;
  onSettings?: () => void;
  onOpenSidebar?: () => void;
};

export function WorkspaceRail({ section, onSectionChange, onSettings, onOpenSidebar }: WorkspaceRailProps) {
  return (
    <nav className="workspace-rail" data-testid="workspace-rail" data-motion="workspace-panel" aria-label="工作区导航">
      <img className="brand-mark" src={catBrandMark} alt="LAN Chat 猫咪标识" />
      <div className="rail-actions">
        <button className={`rail-button ${section === 'rooms' ? 'rail-button--active' : ''}`} type="button" aria-label="群" title="群聊" onClick={() => onSectionChange('rooms')}>群</button>
        <button className={`rail-button ${section === 'direct' ? 'rail-button--active' : ''}`} type="button" aria-label="私" title="私信" onClick={() => onSectionChange('direct')}>私</button>
        {onOpenSidebar && <button className="rail-button rail-button--sidebar" type="button" aria-label="打开导航" title="打开导航" onClick={onOpenSidebar}>☰</button>}
      </div>
      <button className="rail-button rail-button--bottom" type="button" aria-label="设置" title="设置" onClick={onSettings}>⚙</button>
    </nav>
  );
}
