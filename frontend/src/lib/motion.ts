import { useEffect, useState } from 'react';

/**
 * Motion gate shared by CSS and Framer Motion (spec §11):
 *  - html[data-effects='off'] — the native performance profile turned effects off → no motion at all
 *  - prefers-reduced-motion   — keep fades, drop travel and scale (Framer: reducedMotion="user")
 */
function prefersReducedMotion() {
  return typeof window !== 'undefined' && typeof window.matchMedia === 'function'
    && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
}

export function effectsDisabled(): boolean {
  return typeof document !== 'undefined' && document.documentElement.dataset.effects === 'off';
}

/** True when travel/scale animations may run. */
export function motionAllowed(): boolean {
  return !prefersReducedMotion() && !effectsDisabled();
}

/** Spec easing and durations, in seconds for Framer Motion. */
export const ease = [0.2, 0.8, 0.2, 1] as const;
export const duration = { hover: 0.15, enter: 0.2, panel: 0.3 } as const;

/** Duration multiplier published by the native performance profile. */
let motionScale = 1;
export function setMotionScale(scale: number | undefined) {
  motionScale = typeof scale === 'number' && Number.isFinite(scale) ? Math.min(2, Math.max(0.25, scale)) : 1;
  if (typeof document !== 'undefined') document.documentElement.style.setProperty('--motion-scale', String(motionScale));
}
export function scaled(seconds: number) {
  return effectsDisabled() ? 0 : seconds * motionScale;
}

/** Re-render when the effects gate flips so MotionConfig follows it. */
export function useEffectsDisabled(): boolean {
  const [off, setOff] = useState(effectsDisabled);
  useEffect(() => {
    const observer = new MutationObserver(() => setOff(effectsDisabled()));
    observer.observe(document.documentElement, { attributes: true, attributeFilter: ['data-effects'] });
    return () => observer.disconnect();
  }, []);
  return off;
}
