import type { ReactNode } from 'react';

type ModalSurfaceProps = { title: string; onClose: () => void; children: ReactNode };

export function ModalSurface({ title, onClose, children }: ModalSurfaceProps) {
  return (
    <div className="modal-backdrop" role="presentation" onClick={onClose}>
      <section className="modal-surface" role="dialog" aria-modal="true" aria-label={title} onClick={(event) => event.stopPropagation()}>
        <div className="sidebar-heading"><h2>{title}</h2><button className="icon-button" type="button" aria-label="close" onClick={onClose}>×</button></div>
        {children}
      </section>
    </div>
  );
}
