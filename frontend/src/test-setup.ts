import '@testing-library/jest-dom/vitest';
import { MotionGlobalConfig } from 'framer-motion';
import { vi } from 'vitest';

// Behaviour tests do not wait for animations: Framer Motion jumps straight to the end state.
MotionGlobalConfig.skipAnimations = true;

// jsdom gaps that Radix UI and @tanstack/react-virtual rely on.
class ResizeObserverStub {
  observe() {}
  unobserve() {}
  disconnect() {}
}
if (!('ResizeObserver' in globalThis)) {
  (globalThis as unknown as { ResizeObserver: typeof ResizeObserverStub }).ResizeObserver = ResizeObserverStub;
}
if (!Element.prototype.hasPointerCapture) {
  Element.prototype.hasPointerCapture = () => false;
  Element.prototype.setPointerCapture = () => undefined;
  Element.prototype.releasePointerCapture = () => undefined;
}
if (!Element.prototype.scrollIntoView) Element.prototype.scrollIntoView = () => undefined;
if (typeof window.matchMedia !== 'function') {
  // Default to a wide desktop window with motion allowed; tests that need a breakpoint override this.
  window.matchMedia = (query: string) => ({
    matches: false, media: query, onchange: null,
    addEventListener: () => undefined, removeEventListener: () => undefined,
    addListener: () => undefined, removeListener: () => undefined, dispatchEvent: () => false
  }) as MediaQueryList;
}

// jsdom has no layout: scrollTo just moves scrollTop and reports a scroll, which is what the list logic observes.
{
  Element.prototype.scrollTo = function scrollTo(this: Element, options?: ScrollToOptions | number, y?: number) {
    const top = typeof options === 'number' ? y : options?.top;
    if (typeof top === 'number') this.scrollTop = top;
    this.dispatchEvent(new Event('scroll'));
  } as Element['scrollTo'];
}

// emoji-mart is a large web component; tests get a tiny picker with the same selection contract.
vi.mock('emoji-mart', () => ({
  Picker: class FakePicker {
    constructor(options: { onEmojiSelect: (emoji: { native: string }) => void }) {
      const element = document.createElement('div');
      element.setAttribute('data-testid', 'emoji-mart');
      for (const native of ['😀', '👍']) {
        const button = document.createElement('button');
        button.type = 'button';
        button.setAttribute('aria-label', native);
        button.textContent = native;
        button.addEventListener('click', () => options.onEmojiSelect({ native }));
        element.appendChild(button);
      }
      return element;
    }
  }
}));
vi.mock('@emoji-mart/data', () => ({ default: {} }));
vi.mock('@emoji-mart/data/i18n/zh.json', () => ({ default: {} }));
