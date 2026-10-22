/**
 * Stable per-name colours. The palette lives in tokens.css as --avatar-0..7 (gradient pairs)
 * and --name-0..7 (sender-name text colours, all >= 4.5:1 on the incoming bubble).
 */
export const avatarToneCount = 8;

export function nameHash(seed: string): number {
  let hash = 0;
  for (let index = 0; index < seed.length; index += 1) hash = (hash * 31 + seed.charCodeAt(index)) >>> 0;
  return hash;
}

export function avatarTone(seed: string): number {
  return nameHash(seed) % avatarToneCount;
}

/** First visible character, upper-cased for Latin names. */
export function initialOf(name: string): string {
  const trimmed = name.trim();
  return trimmed ? Array.from(trimmed)[0].toUpperCase() : '?';
}
