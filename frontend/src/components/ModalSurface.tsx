import type { ReactNode } from 'react';

type ModalSurfaceProps = { title: string; onClose: () => void; dismissible?: boolean; children: ReactNode };

export function ModalSurface({ title, onClose, dismissible = true, children }: ModalSurfaceProps) {
  return (
    <div className="modal-backdrop" role="presentation" onClick={dismissible ? onClose : undefined}>
      <section className="modal-surface" role="dialog" aria-modal="true" aria-label={title} onClick={(event) => event.stopPropagation()}>
        <div className="sidebar-heading"><h2>{title}</h2>{dismissible && <button className="icon-button" type="button" aria-label="close" onClick={onClose}>×</button>}</div>
        {children}
      </section>
    </div>
  );
}
