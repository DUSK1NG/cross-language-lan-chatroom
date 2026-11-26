import * as TabsPrimitive from '@radix-ui/react-tabs';

import type { ConversationFilter } from './conversations';

const tabs: { value: ConversationFilter; label: string }[] = [
  { value: 'all', label: '全部' },
  { value: 'rooms', label: '群聊' },
  { value: 'direct', label: '私聊' }
];

/** 会话筛选的选中底板跟随当前标签。 */
export function FilterTabs({ value, onValueChange, counts }: { value: ConversationFilter; onValueChange: (value: ConversationFilter) => void; counts?: Partial<Record<ConversationFilter, number>> }) {
  return (
    <TabsPrimitive.Root value={value} onValueChange={(next) => onValueChange(next as ConversationFilter)}>
      <TabsPrimitive.List aria-label="会话筛选" className="conversation-filter relative isolate mx-3 mb-2 grid grid-cols-3 gap-1">
        {tabs.map((tab) => {
          const count = counts?.[tab.value];
          return (
            <TabsPrimitive.Trigger key={tab.value} value={tab.value}
              className="press flex h-7 cursor-pointer items-center justify-center gap-1 rounded-md text-small font-medium text-ink-3 hover:bg-hover hover:text-ink aria-selected:font-semibold aria-selected:text-ink">
              {tab.label}
              {count ? <span className="num text-small text-ink-3">{count}</span> : null}
            </TabsPrimitive.Trigger>
          );
        })}
      </TabsPrimitive.List>
    </TabsPrimitive.Root>
  );
}
