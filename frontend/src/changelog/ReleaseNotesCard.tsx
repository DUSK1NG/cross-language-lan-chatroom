import { useEffect, useLayoutEffect, useRef } from 'react';

import catBrandMark from '../assets/lan-chat-cat.png';
import { animateDialog, animateStagger } from '../animation/motion';
import { Icon, type IconName } from '../components/Icon';
import { currentRelease, type ReleaseNotes, type ReleaseSectionKind } from './releaseNotes';

const sectionIcons: Record<ReleaseSectionKind, IconName> = { new: 'plus', improved: 'sliders', fixed: 'check' };

type ReleaseNotesCardProps = { notes?: ReleaseNotes; onClose(): void };

/** Discord 风格的“新版本”卡片：动画横幅 + 彩色分节 + 逐条入场。 */
export function ReleaseNotesCard({ notes = currentRelease, onClose }: ReleaseNotesCardProps) {
  const cardRef = useRef<HTMLElement>(null);
  const closeRef = useRef<HTMLButtonElement>(null);

  useLayoutEffect(() => {
    const stopDialog = animateDialog(cardRef.current);
    const stopItems = animateStagger(cardRef.current, '[data-release-item]', { delay: 0.18, distance: 10 });
    return () => { stopDialog(); stopItems(); };
  }, []);

  useEffect(() => {
    closeRef.current?.focus();
    const onKey = (event: KeyboardEvent) => { if (event.key === 'Escape') onClose(); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose]);

  const titleId = `release-title-${notes.version}`;
  return (
    <div className="release-backdrop" role="presentation" onClick={onClose}>
      <section ref={cardRef} className="release-card" role="dialog" aria-modal="true" aria-labelledby={titleId}
        data-testid="release-notes-card" onClick={(event) => event.stopPropagation()}>
        <div className="release-banner" aria-hidden="true">
          <span className="release-banner__orb release-banner__orb--a" />
          <span className="release-banner__orb release-banner__orb--b" />
          <span className="release-banner__orb release-banner__orb--c" />
          <span className="release-banner__grid" />
          {[0, 1, 2, 3, 4, 5].map((index) => <span key={index} className={`release-banner__spark release-banner__spark--${index}`} />)}
          <span className="release-banner__shine" />
          <img className="release-banner__mark" src={catBrandMark} alt="" />
          <span className="release-banner__version">v{notes.version}</span>
        </div>
        <button ref={closeRef} className="release-card__close" type="button" aria-label="关闭版本说明" onClick={onClose}><Icon name="close" /></button>
        <div className="release-card__body">
          <p className="release-card__eyebrow">新版本 · {notes.codename} · {notes.date}</p>
          <h2 id={titleId} className="release-card__title">{notes.headline}</h2>
          <p className="release-card__summary">{notes.summary}</p>
          {notes.sections.map((section) => (
            <section key={section.kind} className={`release-section release-section--${section.kind}`} aria-label={section.title}>
              <h3 className="release-section__title"><Icon name={sectionIcons[section.kind]} size={13} />{section.title}</h3>
              <ul>
                {section.items.map((item) => (
                  <li key={item.title} data-release-item className="release-item">
                    <strong>{item.title}</strong>
                    <span>{item.detail}</span>
                  </li>
                ))}
              </ul>
            </section>
          ))}
        </div>
        <footer className="release-card__footer">
          <span>LAN Chat {notes.version}</span>
          <button className="primary-button release-card__cta" type="button" onClick={onClose}>知道了</button>
        </footer>
      </section>
    </div>
  );
}
