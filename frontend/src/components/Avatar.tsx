const toneCount = 8;

/** Stable 0..7 colour index so the same user always gets the same avatar colour. */
export function avatarTone(seed: string): number {
  let hash = 0;
  for (let index = 0; index < seed.length; index += 1) hash = (hash * 31 + seed.charCodeAt(index)) >>> 0;
  return hash % toneCount;
}

type AvatarProps = { name: string; seed?: string; size?: 'tiny' | 'small' | 'regular'; className?: string };

export function Avatar({ name, seed, size = 'regular', className = '' }: AvatarProps) {
  const sizeClass = size === 'regular' ? '' : ` avatar--${size}`;
  return (
    <span className={`avatar avatar--tone-${avatarTone(seed || name)}${sizeClass}${className ? ` ${className}` : ''}`} aria-hidden="true">
      {name.slice(0, 1).toUpperCase()}
    </span>
  );
}
