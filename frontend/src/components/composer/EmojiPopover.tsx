import { useEffect, useRef, useState } from 'react';
import { motion, useIsPresent } from 'framer-motion';

import { ease, easeExit, scaled } from '../../lib/motion';
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
      const pressStyles = document.createElement('style');
      pressStyles.textContent = `
        button:not(:disabled), #nav button:not(:disabled) {
          transition: scale var(--press-duration, var(--dur-press-release)) var(--press-ease, var(--ease-spring)),
            color var(--dur-hover) var(--ease);
        }
        button:active:not(:disabled, [aria-disabled='true']) {
          scale: var(--press-icon-scale);
          --press-duration: var(--dur-press);
          --press-ease: ease-out;
        }
      `;
      picker.shadowRoot?.append(pressStyles);
      hostRef.current.appendChild(picker);
      setLoaded(true);
    });
    return () => { cancelled = true; picker?.remove(); };
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
      exit={{ opacity: 0, y: 6, scale: 0.97, transition: { duration: scaled(0.13), ease: easeExit } }}
      transition={{ duration: scaled(0.2), ease }}
      className="surface-glass absolute bottom-full right-0 z-30 mb-2 origin-bottom-right overflow-hidden rounded-card shadow-dialog">
      {!loaded && <div className="flex h-[360px] w-[352px] flex-col gap-2 p-4"><Skeleton className="h-9 w-full" /><Skeleton className="h-full w-full" /></div>}
      <div ref={hostRef} className="emoji-host" />
    </motion.div>
  );
}
