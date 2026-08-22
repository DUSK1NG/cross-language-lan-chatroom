import { afterEach, describe, expect, it, vi } from 'vitest';

const { context, fromTo } = vi.hoisted(() => ({
  context: vi.fn((callback: () => void) => {
    callback();
    return { revert: vi.fn() };
  }),
  fromTo: vi.fn(() => ({ kill: vi.fn() }))
}));

vi.mock('gsap', () => ({ gsap: { context, fromTo } }));

import { animateWorkspacePanels } from './motion';

describe('workspace return motion budget', () => {
  afterEach(() => {
    context.mockClear();
    fromTo.mockClear();
  });

  it('animates one workspace surface without staggered panel tweens', () => {
    const root = document.createElement('main');
    root.innerHTML = `
      <aside data-motion="workspace-panel"></aside>
      <section data-motion="workspace-panel"></section>
      <section data-motion="workspace-panel"></section>
    `;

    animateWorkspacePanels(root);

    expect(fromTo).toHaveBeenCalledTimes(1);
    expect(fromTo).toHaveBeenCalledWith(
      root,
      { opacity: 0, transform: 'translateY(4px)' },
      expect.objectContaining({
        opacity: 1,
        transform: 'translateY(0px)',
        duration: 0.16,
        ease: 'power3.out',
        force3D: true,
        overwrite: 'auto'
      })
    );
    const animationVars = (fromTo.mock.calls[0] as unknown[] | undefined)?.[2];
    expect(animationVars).not.toHaveProperty('stagger');
  });
});
