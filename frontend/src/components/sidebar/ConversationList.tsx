import { useCallback, useRef } from 'react';
import { useVirtualizer } from '@tanstack/react-virtual';

import type { ConversationRef } from '../../bridge/types';
import { ConversationRow } from './ConversationRow';
import { isActive, type ConversationEntry } from './conversations';

const rowHeight = 64;

type ConversationListProps = {
  entries: ConversationEntry[];
  active: ConversationRef | undefined;
  onSelect: (entry: ConversationEntry) => void;
  label: string;
};

/**
 * Virtualised list (spec §3, §13). Rows are a fixed 64px so no measuring is needed.
 * jsdom has no layout: when the scroller reports 0 height the virtualiser renders
 * nothing, so we fall back to rendering every row (lists stay small in tests).
 */
export function ConversationList({ entries, active, onSelect, label }: ConversationListProps) {
  const scrollRef = useRef<HTMLDivElement>(null);
  const virtualizer = useVirtualizer({
    count: entries.length,
    getScrollElement: () => scrollRef.current,
    estimateSize: () => rowHeight,
    overscan: 8,
    getItemKey: (index) => entries[index].key
  });
  const select = useCallback(onSelect, [onSelect]);
  const items = virtualizer.getVirtualItems();
  const measured = items.length > 0;

  return (
    <div ref={scrollRef} role="list" aria-label={label} className="scroll-thin min-h-0 flex-1 overflow-y-auto px-2 pb-2">
      {measured ? (
        <div className="relative w-full" style={{ height: virtualizer.getTotalSize() }}>
          {items.map((item) => {
            const entry = entries[item.index];
            return (
              <div key={item.key} role="listitem" className="absolute left-0 top-0 w-full" style={{ transform: `translateY(${item.start}px)`, height: rowHeight }}>
                <ConversationRow entry={entry} active={isActive(entry, active)} onSelect={select} />
              </div>
            );
          })}
        </div>
      ) : entries.map((entry) => (
        <div key={entry.key} role="listitem"><ConversationRow entry={entry} active={isActive(entry, active)} onSelect={select} /></div>
      ))}
    </div>
  );
}
