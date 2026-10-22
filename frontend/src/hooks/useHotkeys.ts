import { useEffect, useRef } from 'react';

export type HotkeyMap = Partial<Record<'focusSearch' | 'searchChat' | 'prevConversation' | 'nextConversation' | 'escape' | 'editLast', (event: KeyboardEvent) => void>>;

/**
 * Global shortcuts (spec §12). Handlers are read through a ref so callers may pass
 * fresh closures every render without re-binding the listener.
 *   Ctrl+K  focus the conversation search     Ctrl+F  search the current chat
 *   Ctrl+↑/↓ previous / next conversation      Esc     close a layer / cancel reply
 *   Alt+↑   edit the last own message (not supported by the protocol; no-op unless given)
 */
export function useHotkeys(map: HotkeyMap) {
  const ref = useRef(map);
  ref.current = map;
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (event.isComposing) return;
      const ctrl = event.ctrlKey || event.metaKey;
      const handlers = ref.current;
      let handler: ((event: KeyboardEvent) => void) | undefined;
      if (ctrl && !event.altKey && event.key.toLowerCase() === 'k') handler = handlers.focusSearch;
      else if (ctrl && !event.altKey && event.key.toLowerCase() === 'f') handler = handlers.searchChat;
      else if (ctrl && event.key === 'ArrowUp') handler = handlers.prevConversation;
      else if (ctrl && event.key === 'ArrowDown') handler = handlers.nextConversation;
      else if (event.altKey && !ctrl && event.key === 'ArrowUp') handler = handlers.editLast;
      else if (event.key === 'Escape') handler = handlers.escape;
      if (!handler) return;
      handler(event);
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);
}
