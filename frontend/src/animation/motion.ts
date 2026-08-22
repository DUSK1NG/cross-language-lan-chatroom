import { gsap } from 'gsap';

function prefersReducedMotion() {
  return typeof window !== 'undefined' && typeof window.matchMedia === 'function'
    && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
}

export function animateWorkspacePanels(root: Element) {
  if (prefersReducedMotion()) return () => undefined;
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

export function animateMessage(element: HTMLElement | null) {
  if (!element || prefersReducedMotion()) return () => undefined;
  const tween = gsap.fromTo(element,
    { opacity: 0, transform: 'translateY(6px)' },
    { opacity: 1, transform: 'translateY(0px)', duration: 0.18, ease: 'power3.out', clearProps: 'transform,opacity' }
  );
  return () => { tween.kill(); };
}

export function animatePopover(element: HTMLElement | null) {
  if (!element || prefersReducedMotion()) return () => undefined;
  const tween = gsap.fromTo(element,
    { opacity: 0, transform: 'translateY(4px) scale(0.96)', transformOrigin: 'bottom right' },
    { opacity: 1, transform: 'translateY(0px) scale(1)', duration: 0.18, ease: 'power3.out', clearProps: 'transform,opacity,transformOrigin' }
  );
  return () => { tween.kill(); };
}
