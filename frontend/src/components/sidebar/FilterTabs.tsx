import * as TabsPrimitive from '@radix-ui/react-tabs';
import { motion } from 'framer-motion';

import { cn } from '../../lib/cn';
import type { ConversationFilter } from './conversations';

const tabs: { value: ConversationFilter; label: string }[] = [
  { value: 'all', label: '全部' },
  { value: 'rooms', label: '群聊' },
  { value: 'direct', label: '私聊' }
];

/** 全部 / 群聊 / 私聊 with a sliding underline (spec §3). Radix Tabs gives role="tab" + aria-selected. */
export function FilterTabs({ value, onValueChange, counts }: { value: ConversationFilter; onValueChange: (value: ConversationFilter) => void; counts?: Partial<Record<ConversationFilter, number>> }) {
  return (
    <TabsPrimitive.Root value={value} onValueChange={(next) => onValueChange(next as ConversationFilter)}>
      <TabsPrimitive.List aria-label="会话筛选" className="flex border-b border-divider px-2">
        {tabs.map((tab) => {
          const selected = tab.value === value;
          const count = counts?.[tab.value];
          return (
            <TabsPrimitive.Trigger key={tab.value} value={tab.value}
              className={cn('press relative flex h-9 flex-1 cursor-pointer items-center justify-center gap-1 text-body font-medium',
                selected ? 'text-accent-text' : 'text-ink-2 hover:text-ink')}>
              {tab.label}
              {count ? <span className="num text-small text-ink-3">{count}</span> : null}
              {selected && (
                <motion.span layoutId="filter-underline" aria-hidden="true"
                  className="absolute bottom-0 left-3 right-3 h-[2px] rounded-full bg-accent"
                  transition={{ duration: 0.2, ease: [0.2, 0.8, 0.2, 1] }} />
              )}
            </TabsPrimitive.Trigger>
          );
        })}
      </TabsPrimitive.List>
    </TabsPrimitive.Root>
  );
}
