import { useEffect, useRef, useState } from 'react';
import { motion, useIsPresent } from 'framer-motion';

import { duration, ease, easeExit, effectsDisabled, scaled } from '../../lib/motion';
import { Skeleton } from '../ui/states';

type EmojiPopoverProps = { onSelect: (emoji: string) => void; onClose: (restoreFocus: boolean) => void; theme: 'dark' | 'light' };

/**
 * emoji-mart picker above the emoji button (spec §8). emoji-mart's React wrapper only
 * declares React 16–18, so the framework-free web component is used directly; it and
 * its data are code-split and loaded on first open. Esc or a click outside closes it.
 */
export function EmojiPopover({ onSelect, onClose, theme }: EmojiPopoverProps) {
  const hostRef = useRef<HTMLDivElement>(null);
  const [loaded, setLoaded] = useState(false);
  const isPresent = useIsPresent();
  const selectRef = useRef(onSelect);
  selectRef.current = onSelect;

  useEffect(() => {
    let cancelled = false;
    let picker: HTMLElement | null = null;
    let categoryMotion: Animation | undefined;
    const categoryEvents = new AbortController();
    void Promise.all([import('emoji-mart'), import('@emoji-mart/data'), import('@emoji-mart/data/i18n/zh.json')]).then(([{ Picker }, data, zh]) => {
      if (cancelled || !hostRef.current) return;
      picker = new Picker({
        data: data.default ?? data,
        i18n: zh.default ?? zh,
        theme,
        set: 'native',
        previewPosition: 'none',
        skinTonePosition: 'search',
        navPosition: 'top',
        perLine: 8,
        maxFrequentRows: 2,
        autoFocus: true,
        onEmojiSelect: (emoji: { native: string }) => selectRef.current(emoji.native)
      }) as unknown as HTMLElement;
      const pickerStyles = document.createElement('style');
      pickerStyles.textContent = `
        button:not(:disabled), #nav button:not(:disabled) {
          transition: scale var(--press-duration, var(--dur-press-release)) var(--press-ease, var(--ease-spring)),
            color var(--dur-hover) var(--ease);
        }
        button:active:not(:disabled, [aria-disabled='true']) {
          scale: var(--press-icon-scale);
          --press-duration: var(--dur-press);
          --press-ease: ease-out;
        }
        #nav .bar {
          transition: transform var(--emoji-category-duration) var(--ease),
            opacity var(--emoji-category-duration) var(--ease);
        }
      `;
      const shadow = picker.shadowRoot;
      if (!shadow) throw new Error('Emoji picker did not create its shadow root');
      shadow.append(pickerStyles);
      shadow.addEventListener('click', (event) => {
        const tab = event.target instanceof Element ? event.target.closest('#nav button') : null;
        if (!tab || tab.getAttribute('aria-selected') === 'true' || effectsDisabled()) return;
        const grid = shadow.querySelector('.scroll');
        if (!grid) throw new Error('Emoji picker category grid is unavailable');
        categoryMotion?.cancel();
        categoryMotion = grid.animate([{ opacity: 0.55, transform: 'translateY(4px)' }, { opacity: 1, transform: 'none' }],
          { duration: scaled(duration.surface) * 1000, easing: `cubic-bezier(${ease.join(',')})` });
      }, { capture: true, signal: categoryEvents.signal });
      hostRef.current.appendChild(picker);
      setLoaded(true);
    });
    return () => {
      cancelled = true;
      categoryEvents.abort();
      categoryMotion?.cancel();
      picker?.remove();
    };
  }, [theme]);

  useEffect(() => {
    if (!isPresent) return;
    const onKey = (event: KeyboardEvent) => { if (event.key === 'Escape') { event.stopPropagation(); onClose(true); } };
    const onPointer = (event: PointerEvent) => {
      const host = hostRef.current?.parentElement;
      if (host && !host.contains(event.target as Node) && !(event.target as Element).closest?.('[data-emoji-trigger]')) onClose(false);
    };
    window.addEventListener('keydown', onKey, true);
    window.addEventListener('pointerdown', onPointer);
    return () => { window.removeEventListener('keydown', onKey, true); window.removeEventListener('pointerdown', onPointer); };
  }, [isPresent, onClose]);

  return (
    <motion.div role="dialog" aria-label="表情选择器" inert={!isPresent} aria-hidden={!isPresent || undefined}
      initial={{ opacity: 0, y: 6, scale: 0.97 }} animate={{ opacity: 1, y: 0, scale: 1 }}
      exit={{ opacity: 0, y: 6, scale: 0.97, transition: { duration: scaled(duration.exit), ease: easeExit } }}
      transition={{ duration: scaled(duration.open), ease, opacity: { duration: scaled(duration.surface), ease } }}
      className="surface-glass absolute bottom-full right-0 z-30 mb-2 origin-bottom-right overflow-hidden rounded-[12px] border border-divider bg-sidebar shadow-dialog">
      {!loaded && <div className="flex h-[360px] w-[352px] flex-col gap-2 p-4"><Skeleton className="h-9 w-full" /><Skeleton className="h-full w-full" /></div>}
      <div ref={hostRef} className="emoji-host" />
    </motion.div>
  );
}
