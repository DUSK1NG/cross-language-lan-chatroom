import { useEffect, useRef, useState } from 'react';
import { motion, useIsPresent } from 'framer-motion';

import { duration, ease, easeExit, scaled } from '../../lib/motion';
import { Skeleton } from '../ui/states';

type EmojiPopoverProps = { onSelect: (emoji: string) => void; onClose: (restoreFocus: boolean) => void; theme: 'dark' | 'light' };

type PickerRows = Record<number, boolean>;
// emoji-mart 5.6 exposes its mounted Preact component on the custom element.
type EmojiMartElement = HTMLElement & {
  component: {
    props: { emojiButtonSize: number };
    state: { searchResults: unknown[] | null; visibleRows: PickerRows };
    refs: {
      categories: Map<string, { root: { current: HTMLElement }; rows: { index: number; current?: Element | null }[] }>;
      scroll: { current: HTMLElement };
      navigation: { current: { categories: { id: string }[]; setState: (state: { categoryId: string }) => void } };
    };
    setState: (update: (state: { visibleRows: PickerRows }) => { visibleRows: PickerRows }, onCommit: () => void) => void;
    ignoreMouse: () => void;
  };
};

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
    let picker: EmojiMartElement | null = null;
    let categoryRequest = 0;
    void Promise.all([import('emoji-mart'), import('@emoji-mart/data'), import('@emoji-mart/data/i18n/zh.json')]).then(([{ Picker }, data, zh]) => {
      if (cancelled || !hostRef.current) return;
      const element = new Picker({
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
      }) as unknown as EmojiMartElement;
      picker = element;
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
          display: none;
        }
        #nav button:not(:disabled) {
          position: relative;
          transition-property: scale;
        }
        #nav button[aria-selected='true']::after {
          content: '';
          position: absolute;
          inset-inline: 0;
          bottom: -12px;
          height: 3px;
          border-radius: 3px 3px 0 0;
          background-color: currentColor;
          pointer-events: none;
          animation: emoji-category-activate var(--emoji-category-duration) var(--ease);
        }
        @keyframes emoji-category-activate {
          from { opacity: 0.65; transform: scaleX(0.75); }
          to { opacity: 1; transform: scaleX(1); }
        }
        @media (prefers-reduced-motion: reduce) {
          #nav button[aria-selected='true']::after { animation: none; }
        }
      `;
      const shadow = picker.shadowRoot;
      if (!shadow) throw new Error('Emoji picker did not create its shadow root');
      shadow.append(pickerStyles);
      shadow.addEventListener('click', (event) => {
        const tab = event.target instanceof Element ? event.target.closest('#nav button') : null;
        if (!tab) return;
        const component = element.component;
        if (component.state.searchResults) return;

        const { categories, navigation: { current: navigation }, scroll: { current: scroll } } = component.refs;
        const index = Array.from(shadow.querySelectorAll('#nav button')).indexOf(tab);
        const category = navigation.categories[index];
        const section = categories.get(category.id);
        if (!section) throw new Error(`Missing emoji picker section: ${category.id}`);

        // The picker observes the first row of each virtual group. Mount the groups
        // covering the destination viewport before its native scroll jumps there.
        const firstRow = section.rows[0].index;
        const lastRow = firstRow + Math.ceil(scroll.clientHeight / component.props.emojiButtonSize) - 1;
        const anchors = [...categories.values()].flatMap(({ rows }) => rows.filter((row) => 'current' in row).map((row) => row.index));
        const groups = anchors.filter((row, i) => row <= lastRow && (anchors[i + 1] ?? Infinity) > firstRow);
        event.stopPropagation();
        const request = ++categoryRequest;
        const navigate = () => {
          if (cancelled || request !== categoryRequest) return;
          navigation.setState({ categoryId: category.id });
          component.ignoreMouse();
          // Both offsets use the same layout origin and are independent of the popover's entry scale.
          scroll.scrollTop = index === 0 ? 0 : section.root.current.offsetTop - scroll.offsetTop + 1;
        };
        const missingGroups = groups.filter((row) => !component.state.visibleRows[row]);
        if (missingGroups.length === 0) {
          navigate();
          return;
        }
        component.setState(({ visibleRows }) => ({
          visibleRows: { ...visibleRows, ...Object.fromEntries(missingGroups.map((row) => [row, true])) }
        }), navigate);
      }, { capture: true });
      hostRef.current.appendChild(picker);
      setLoaded(true);
    });
    return () => {
      cancelled = true;
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
