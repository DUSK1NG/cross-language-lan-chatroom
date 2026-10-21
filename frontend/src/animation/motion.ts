import { gsap } from 'gsap';

function prefersReducedMotion() {
  return typeof window !== 'undefined' && typeof window.matchMedia === 'function'
    && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
}

export function motionAllowed(): boolean {
  if (prefersReducedMotion()) return false;
  return typeof document === 'undefined' || document.documentElement.dataset.effects !== 'off';
}

/**
 * Global duration multiplier published by the native performance profile
 * (`performance.animationDurationScale`). Clamped so a bad value can never
 * freeze or stretch the UI.
 */
let durationScale = 1;
export function setMotionScale(scale: number | undefined) {
  durationScale = typeof scale === 'number' && Number.isFinite(scale) ? Math.min(2, Math.max(0.25, scale)) : 1;
  if (typeof document !== 'undefined') document.documentElement.style.setProperty('--t-motion-scale', String(durationScale));
}
const d = (seconds: number) => seconds * durationScale;

export function animateWorkspacePanels(root: Element) {
  if (!motionAllowed()) return () => undefined;
  const context = gsap.context(() => {
    gsap.fromTo(root,
      { opacity: 0, transform: 'translateY(4px)' },
      {
        opacity: 1,
        transform: 'translateY(0px)',
        duration: 0.16,
        ease: 'power3.out',
        force3D: true,
        overwrite: 'auto',
        clearProps: 'transform,opacity'
      }
    );
  }, root);
  return () => { context.revert(); };
}

/** Message entry: own messages slide in from the right, peers from the left, system lines just fade. */
export function animateMessage(element: HTMLElement | null, side: 'self' | 'peer' | 'system' = 'peer') {
  if (!element || !motionAllowed()) return () => undefined;
  const from = side === 'system'
    ? { opacity: 0, transform: 'translateY(4px)' }
    : { opacity: 0, transform: `translate(${side === 'self' ? 10 : -10}px, 6px) scale(0.98)` };
  const tween = gsap.fromTo(element, from,
    { opacity: 1, transform: 'translate(0px, 0px) scale(1)', duration: d(0.22), ease: 'power3.out', clearProps: 'transform,opacity' }
  );
  return () => { tween.kill(); };
}

export function animatePopover(element: HTMLElement | null) {
  if (!element || !motionAllowed()) return () => undefined;
  const tween = gsap.fromTo(element,
    { opacity: 0, transform: 'translateY(4px) scale(0.96)', transformOrigin: 'bottom right' },
    { opacity: 1, transform: 'translateY(0px) scale(1)', duration: d(0.18), ease: 'power3.out', clearProps: 'transform,opacity,transformOrigin' }
  );
  return () => { tween.kill(); };
}

/** Full-page entrance used when switching between mode / connect / host / settings pages. */
export function animatePage(root: HTMLElement | null) {
  if (!root || !motionAllowed()) return () => undefined;
  const tween = gsap.fromTo(root,
    { opacity: 0, transform: 'translateY(10px)' },
    { opacity: 1, transform: 'translateY(0px)', duration: d(0.28), ease: 'power3.out', clearProps: 'transform,opacity' }
  );
  return () => { tween.kill(); };
}

/** One-shot staggered entrance for a short list of elements (welcome cards, release-note items). */
export function animateStagger(root: HTMLElement | null, selector: string, options: { delay?: number; distance?: number } = {}) {
  if (!root || !motionAllowed()) return () => undefined;
  const targets = Array.from(root.querySelectorAll<HTMLElement>(selector)).slice(0, 12);
  if (targets.length === 0) return () => undefined;
  const tween = gsap.fromTo(targets,
    { opacity: 0, transform: `translateY(${options.distance ?? 12}px)` },
    {
      opacity: 1, transform: 'translateY(0px)', duration: d(0.36), ease: 'power3.out',
      delay: d(options.delay ?? 0), stagger: d(0.06), clearProps: 'transform,opacity'
    }
  );
  return () => { tween.kill(); };
}

/** Discord-style dialog pop: spring-ish scale-up from slightly below. */
export function animateDialog(element: HTMLElement | null) {
  if (!element || !motionAllowed()) return () => undefined;
  const tween = gsap.fromTo(element,
    { opacity: 0, transform: 'translateY(16px) scale(0.94)' },
    { opacity: 1, transform: 'translateY(0px) scale(1)', duration: d(0.42), ease: 'back.out(1.6)', clearProps: 'transform,opacity' }
  );
  return () => { tween.kill(); };
}
