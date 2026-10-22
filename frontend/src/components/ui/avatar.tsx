import { memo } from 'react';

import { avatarTone, initialOf } from '../../lib/avatarColor';
import { cn } from '../../lib/cn';

type AvatarProps = {
  name: string;
  /** Hash seed for the colour; defaults to the name (userCode or room name keep it stable across renames). */
  seed?: string;
  size?: 22 | 32 | 36 | 40 | 48 | 80;
  /** Shows a 12px presence dot with a 2px ring in the given surface colour token. */
  online?: boolean;
  ringClass?: string;
  className?: string;
};

/**
 * Circle avatar: the name's initial on a fixed gradient chosen by hashing the name (spec §3).
 * Decorative: the owning row carries the accessible name.
 */
export const Avatar = memo(function Avatar({ name, seed, size = 48, online, ringClass = 'ring-sidebar', className }: AvatarProps) {
  const tone = avatarTone(seed || name);
  const fontSize = size >= 80 ? 32 : size >= 48 ? 19 : size >= 36 ? 15 : size >= 32 ? 13 : 10;
  return (
    <span aria-hidden="true" className={cn('relative inline-flex shrink-0', className)} style={{ width: size, height: size }}>
      <span className="inline-flex size-full items-center justify-center rounded-full font-medium text-on-accent select-none"
        style={{ fontSize, background: `linear-gradient(180deg, var(--avatar-${tone}-from), var(--avatar-${tone}-to))` }}>
        {initialOf(name)}
      </span>
      {online !== undefined && (
        <span data-presence={online ? 'online' : 'offline'}
          className={cn('absolute bottom-0 right-0 size-3 rounded-full ring-2', ringClass, online ? 'bg-online' : 'hidden')} />
      )}
    </span>
  );
});
