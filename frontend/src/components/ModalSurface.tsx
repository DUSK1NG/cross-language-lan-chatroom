import type { ReactNode } from 'react';
import { Icon } from './Icon';

type ModalSurfaceProps = {
  title: string;
  onClose: () => void;
  dismissible?: boolean;
  /** `card` hides the text header (title stays the accessible name) so content can start with a banner. */
  variant?: 'default' | 'card';
  children: ReactNode;
};

export function ModalSurface({ title, onClose, dismissible = true, variant = 'default', children }: ModalSurfaceProps) {
  const card = variant === 'card';
  return (
    <div className="modal-backdrop" role="presentation" onClick={dismissible ? onClose : undefined}>
      <section className={`modal-surface${card ? ' modal-surface--card' : ''}`} role="dialog" aria-modal="true" aria-label={title} onClick={(event) => event.stopPropagation()}>
        {card
          ? dismissible && <button className="icon-button modal-surface__float-close" type="button" aria-label="close" onClick={onClose}><Icon name="close" /></button>
          : <div className="modal-surface__header"><h2>{title}</h2>{dismissible && <button className="icon-button" type="button" aria-label="close" onClick={onClose}><Icon name="close" /></button>}</div>}
        {children}
      </section>
    </div>
  );
}
