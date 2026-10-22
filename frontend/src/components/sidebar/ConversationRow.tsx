import { memo } from 'react';
import { Lock, ShieldCheck } from 'lucide-react';

import { Avatar } from '../ui/avatar';
import { cn } from '../../lib/cn';
import { entryLabel, formatUnread, type ConversationEntry } from './conversations';

type ConversationRowProps = { entry: ConversationEntry; active: boolean; onSelect: (entry: ConversationEntry) => void };

/**
 * 64px row: 48px avatar · name over a one-line preview · unread badge (spec §3).
 * The bridge publishes no last-message preview, time, pin or mute, so line 2 shows
 * what is known (member count, private/E2EE, the user code) — see the plan, item 一.4.
 */
export const ConversationRow = memo(function ConversationRow({ entry, active, onSelect }: ConversationRowProps) {
  const label = entryLabel(entry);
  const mls = entry.kind === 'room' ? entry.room.mls : entry.direct.mls;
  const secondary = entry.kind === 'room'
    ? `${entry.room.memberCount} 人${entry.room.private ? ' · 私有' : ''}`
    : `#${entry.direct.userCode}`;
  return (
    <button type="button" aria-label={label} aria-current={active ? 'page' : undefined} onClick={() => onSelect(entry)}
      className={cn('press group flex h-16 w-full cursor-pointer items-center gap-3 rounded-md px-2 text-left',
        active ? 'bg-active' : 'hover:bg-hover')}>
      <Avatar name={entry.title} seed={entry.seed} size={48}
        online={entry.kind === 'dm' ? entry.online === true : undefined}
        ringClass={active ? 'ring-active' : 'ring-sidebar group-hover:ring-hover'} />
      <span className="flex min-w-0 flex-1 flex-col">
        <span className="flex min-w-0 items-center gap-1">
          {entry.kind === 'room' && entry.room.private && <Lock size={14} aria-hidden="true" className={cn('shrink-0', active ? 'text-on-accent' : 'text-ink-2')} />}
          <span className={cn('truncate text-name font-semibold', active ? 'text-on-accent' : 'text-ink')}>{entry.title}</span>
          {mls && (
            <span title="端到端加密" aria-label={`${entry.title} 端到端加密`} className={cn('inline-flex shrink-0', active ? 'text-on-accent' : 'text-ink-3')}>
              <ShieldCheck size={14} aria-hidden="true" />
            </span>
          )}
        </span>
        <span className={cn('truncate text-small', active ? 'text-on-active-2' : 'text-ink-2')}>{secondary}</span>
      </span>
      {entry.unread > 0 && (
        <span className={cn('num inline-flex h-5 min-w-5 shrink-0 items-center justify-center rounded-full px-2 text-small font-medium',
          active ? 'bg-on-accent text-accent-strong' : 'bg-accent-strong text-on-accent')}>
          {formatUnread(entry.unread)}
        </span>
      )}
    </button>
  );
});
