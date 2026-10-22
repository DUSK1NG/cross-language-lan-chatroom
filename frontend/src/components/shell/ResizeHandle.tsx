import { useCallback, useEffect, useRef, useState, type KeyboardEvent, type PointerEvent } from 'react';

export const sidebarMin = 260;
export const sidebarMax = 420;
export const sidebarDefault = 320;
const storageKey = 'lan-chat.sidebarWidth';

const clamp = (value: number) => Math.min(sidebarMax, Math.max(sidebarMin, Math.round(value)));

/** Sidebar width, 260–420px, remembered across launches (spec §2). */
export function useSidebarWidth() {
  const [width, setWidth] = useState(() => {
    const saved = Number(window.localStorage.getItem(storageKey));
    return Number.isFinite(saved) && saved > 0 ? clamp(saved) : sidebarDefault;
  });
  const save = useCallback((next: number) => {
    const value = clamp(next);
    setWidth(value);
    window.localStorage.setItem(storageKey, String(value));
  }, []);
  return [width, save] as const;
}

/** Drag handle on the sidebar's right edge; also operable with ←/→ (16px steps) and Home/End. */
export function ResizeHandle({ width, onWidthChange }: { width: number; onWidthChange: (width: number) => void }) {
  const drag = useRef<{ startX: number; startWidth: number } | null>(null);
  const [dragging, setDragging] = useState(false);

  const onPointerDown = (event: PointerEvent<HTMLDivElement>) => {
    event.preventDefault();
    drag.current = { startX: event.clientX, startWidth: width };
    setDragging(true);
    event.currentTarget.setPointerCapture?.(event.pointerId);
  };
  const onPointerMove = (event: PointerEvent<HTMLDivElement>) => {
    if (!drag.current) return;
    onWidthChange(drag.current.startWidth + event.clientX - drag.current.startX);
  };
  const end = () => { drag.current = null; setDragging(false); };
  const onKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    const step = event.shiftKey ? 48 : 16;
    if (event.key === 'ArrowLeft') onWidthChange(width - step);
    else if (event.key === 'ArrowRight') onWidthChange(width + step);
    else if (event.key === 'Home') onWidthChange(sidebarMin);
    else if (event.key === 'End') onWidthChange(sidebarMax);
    else return;
    event.preventDefault();
  };

  useEffect(() => {
    document.body.style.cursor = dragging ? 'col-resize' : '';
    document.body.style.userSelect = dragging ? 'none' : '';
  }, [dragging]);

  return (
    <div role="separator" aria-orientation="vertical" aria-label="调整会话列表宽度" tabIndex={0}
      aria-valuemin={sidebarMin} aria-valuemax={sidebarMax} aria-valuenow={width}
      onPointerDown={onPointerDown} onPointerMove={onPointerMove} onPointerUp={end} onPointerCancel={end} onKeyDown={onKeyDown}
      onDoubleClick={() => onWidthChange(sidebarDefault)}
      className="group absolute -right-1 top-0 z-20 h-full w-2 cursor-col-resize">
      <span aria-hidden="true" className={`absolute left-1/2 top-0 h-full w-[2px] -translate-x-1/2 transition-colors duration-[var(--dur-hover)] ${dragging ? 'bg-accent' : 'bg-transparent group-hover:bg-accent/60 group-focus-visible:bg-accent'}`} />
    </div>
  );
}
