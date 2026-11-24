import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState, type RefObject } from 'react';
import { useVirtualizer, type Virtualizer } from '@tanstack/react-virtual';
import { animate, AnimatePresence, motion, type AnimationPlaybackControls } from 'framer-motion';
import { ChevronDown, MessageCircle } from 'lucide-react';

import type { BridgeState, ChatBridgeClient, MessageItem } from '../../bridge/types';
import { groupPositions, isRenderable, messageDate } from '../../lib/messageGroups';
import { cn } from '../../lib/cn';
import { ease, effectsDisabled, scaled } from '../../lib/motion';
import { useAppSettings } from '../../state/appSettings';
import { EmptyState, Skeleton } from '../ui/states';
import { MessageBubble, parseQuote } from './MessageBubble';

const bottomThresholdPx = 32;
const estimatedRowPx = 54;
const highlightMs = 1000;
const enterMs = 1020;

type RowPosition = { element: HTMLElement; top: number; messageTop: number; left: number; width: number };
const threadSpring = { type: 'spring', stiffness: 158, damping: 22.1, mass: 1 } as const;

type MessageListProps = {
  state: BridgeState;
  bridge: ChatBridgeClient;
  onQuote?: (message: MessageItem) => void;
  canRecall?: (message: MessageItem) => boolean;
  /** Unread count the conversation had when it was opened: draws 以下为新消息 and scrolls there (spec §5). */
  entryUnread?: number;
  sendAnchor?: RefObject<HTMLFormElement | null>;
};

export function conversationKey(state: BridgeState) {
  const conversation = state.navigation.activeConversation;
  if (!conversation) return '';
  return conversation.kind === 'dm' ? `dm:${conversation.userCode}` : `${conversation.kind}:${conversation.id}`;
}

/** Reads the scroller's client box so the virtualiser works with mocked geometry in jsdom too. */
function observeClientRect(instance: Virtualizer<HTMLDivElement, Element>, callback: (rect: { width: number; height: number }) => void) {
  const element = instance.scrollElement;
  if (!element) return undefined;
  let width = -1;
  let height = -1;
  const update = () => {
    if (element.clientWidth === width && element.clientHeight === height) return;
    width = element.clientWidth;
    height = element.clientHeight;
    callback({ width, height });
  };
  update();
  const observer = new ResizeObserver(update);
  observer.observe(element);
  // Also re-read on scroll: cheap (no layout writes) and covers hosts without ResizeObserver delivery.
  element.addEventListener('scroll', update, { passive: true });
  return () => { observer.disconnect(); element.removeEventListener('scroll', update); };
}

/**
 * Virtualised, bottom-anchored message list (spec §5, §13).
 * - few messages sit at the bottom; the reader at the bottom follows new messages
 * - a reader scrolled up stays put and gets a 回到底部 button with the new-message count
 * - 以下为新消息 marks where unread starts (on entry) or what arrived while scrolled up
 * - only messages appended after the first snapshot animate in; history never does
 */
export function MessageList({ state, bridge, onQuote, canRecall, entryUnread = 0, sendAnchor }: MessageListProps) {
  const settings = useAppSettings();
  const key = conversationKey(state);
  const isGroupChat = state.navigation.activeConversation?.kind !== 'dm';
  const rows = useMemo(() => state.activeMessages.filter(isRenderable), [state.activeMessages]);
  const positions = useMemo(() => groupPositions(rows), [rows]);
  const dateLabels = useMemo(() => {
    let previousDate = '';
    return rows.map(({ createdAt }) => {
      const date = messageDate(createdAt);
      if (!date || date === previousDate) return '';
      previousDate = date;
      return date;
    });
  }, [rows]);

  const scrollRef = useRef<HTMLDivElement>(null);
  const listRef = useRef<HTMLDivElement>(null);
  const exitsRef = useRef<HTMLDivElement>(null);
  const rowLayout = useRef({ key: '', top: 0, scrollTop: 0, rows: new Map<string, RowPosition>() });
  const rowAnimations = useRef(new Map<string, AnimationPlaybackControls>());
  const atBottomRef = useRef(true);
  const knownIdsRef = useRef<Set<string> | null>(null);
  const keyRef = useRef('');
  const [atBottom, setAtBottom] = useState(true);
  const [newCount, setNewCount] = useState(0);
  const [dividerId, setDividerId] = useState<string | null>(null);
  const [entering, setEntering] = useState<ReadonlySet<string>>(() => new Set());
  const [highlightId, setHighlightId] = useState<string | null>(null);
  const [hasLayout, setHasLayout] = useState(false);

  const virtualizer = useVirtualizer({
    count: rows.length,
    getScrollElement: () => scrollRef.current,
    estimateSize: (index) => estimatedRowPx + (dateLabels[index] ? 36 : 0),
    overscan: 10,
    getItemKey: (index) => rows[index].messageId,
    observeElementRect: observeClientRect,
    measureElement: (element) => element.getBoundingClientRect().height || estimatedRowPx,
    scrollMargin: listRef.current?.offsetTop ?? 0
  });

  const checkLayout = useCallback(() => {
    const element = scrollRef.current;
    setHasLayout(Boolean(element && element.clientHeight > 0));
  }, []);

  const markBottom = useCallback((value: boolean) => {
    atBottomRef.current = value;
    setAtBottom(value);
    if (value) setNewCount(0);
  }, []);

  const scrollToBottom = useCallback(() => {
    const element = scrollRef.current;
    if (!element) return;
    element.scrollTop = element.scrollHeight;
    markBottom(true);
    // Dynamic row heights settle after paint; pin to the bottom once more.
    requestAnimationFrame(() => { if (atBottomRef.current && scrollRef.current) scrollRef.current.scrollTop = scrollRef.current.scrollHeight; });
  }, [markBottom]);

  const scrollToMessage = useCallback((messageId: string, align: 'start' | 'center' = 'center') => {
    const index = rows.findIndex((message) => message.messageId === messageId);
    if (index < 0) return false;
    if (hasLayout) virtualizer.scrollToIndex(index, { align });
    else scrollRef.current?.querySelector(`[data-testid="message-${messageId.replace(/["\\]/g, '\\$&')}"]`)?.scrollIntoView({ block: align });
    return true;
  }, [rows, hasLayout, virtualizer]);

  const onScroll = useCallback(() => {
    const element = scrollRef.current;
    if (!element) return;
    checkLayout();
    const distance = element.scrollHeight - element.clientHeight - element.scrollTop;
    const bottom = distance <= bottomThresholdPx;
    if (bottom !== atBottomRef.current) markBottom(bottom);
    else if (bottom) setNewCount(0);
  }, [checkLayout, markBottom]);

  // Conversation switches, first snapshot, and appended messages.
  useLayoutEffect(() => {
    const ids = new Set(rows.map((message) => message.messageId));
    if (keyRef.current !== key || knownIdsRef.current === null) {
      keyRef.current = key;
      knownIdsRef.current = ids;
      setEntering(new Set());
      setNewCount(0);
      const firstUnread = entryUnread > 0 && entryUnread <= rows.length ? rows[rows.length - entryUnread] : undefined;
      setDividerId(firstUnread?.messageId ?? null);
      if (firstUnread) {
        markBottom(false);
        requestAnimationFrame(() => scrollToMessage(firstUnread.messageId, 'start'));
      } else {
        scrollToBottom();
      }
      return;
    }
    const known = knownIdsRef.current;
    const firstNew = rows.findIndex((message) => !known.has(message.messageId));
    knownIdsRef.current = ids;
    if (firstNew < 0) {
      if (ids.size < known.size && atBottomRef.current) scrollToBottom();
      return;
    }
    const appended = rows.slice(firstNew);
    // Only a pure append at the tail counts as "new"; anything else is a history refresh.
    if (appended.some((message) => known.has(message.messageId))) return;
    setEntering((current) => new Set([...current, ...appended.map((message) => message.messageId)]));
    const own = appended.some((message) => message.selfMessage || message.userCode === state.identity.userCode);
    if (own || atBottomRef.current) {
      scrollToBottom();
      return;
    }
    const received = appended.filter((message) => !message.systemMessage);
    if (received.length === 0) return;
    setDividerId((current) => current ?? received[0].messageId);
    setNewCount((count) => count + received.length);
  }, [key, rows, entryUnread, state.identity.userCode, scrollToBottom, scrollToMessage, markBottom]);

  useLayoutEffect(() => {
    const list = listRef.current;
    const scroller = scrollRef.current;
    const exits = exitsRef.current;
    if (!list || !scroller || !exits) return;
    const previous = rowLayout.current;
    const viewport = scroller.getBoundingClientRect();
    const visible = new Map<string, RowPosition>();
    for (const element of list.querySelectorAll<HTMLElement>('[data-cirrus-row]')) {
      const rect = element.getBoundingClientRect();
      if (rect.height === 0) continue;
      const messageTop = element.querySelector('.message')!.getBoundingClientRect().top - viewport.top;
      visible.set(element.dataset.cirrusRow!, {
        element, top: rect.top - viewport.top, messageTop,
        left: rect.left - viewport.left, width: rect.width
      });
    }
    rowLayout.current = { key, top: viewport.top, scrollTop: scroller.scrollTop, rows: visible };
    if (previous.key !== key) {
      exits.replaceChildren();
      return;
    }
    if (effectsDisabled()) return;

    const currentIds = new Set(rows.map((message) => message.messageId));
    const deleted = [...previous.rows].filter(([id]) => !currentIds.has(id));
    for (const [, position] of deleted) {
      const article = position.element.querySelector<HTMLElement>('.message');
      const bubble = article?.querySelector<HTMLElement>('.cirrus-bubble');
      if (!article || !bubble) continue;
      const ghost = article.cloneNode(true) as HTMLElement;
      ghost.querySelectorAll('[data-testid]').forEach((node) => node.removeAttribute('data-testid'));
      ghost.removeAttribute('data-testid');
      ghost.removeAttribute('data-entering');
      ghost.classList.add('cirrus-leaving');
      ghost.inert = true;
      Object.assign(ghost.style, { position: 'absolute', top: `${position.messageTop}px`, left: `${position.left}px`, width: `${position.width}px` });
      exits.append(ghost);
      ghost.addEventListener('animationend', () => ghost.remove(), { once: true });
      window.setTimeout(() => ghost.remove(), 450);
    }

    const distanceFromGap = deleted.length > 0 ? Math.min(...deleted.map(([, row]) => row.top)) : 0;
    let delay = .06;
    for (const [id, current] of [...visible].reverse()) {
      const prior = previous.rows.get(id);
      const inner = current.element.firstElementChild as HTMLElement;
      const scrollDelta = atBottomRef.current ? 0 : previous.scrollTop - scroller.scrollTop;
      if (!prior || Math.abs(previous.top - viewport.top) > 1) continue;
      const displacement = prior.top + scrollDelta - current.top;
      if (Math.abs(displacement) < 1) continue;
      const onScreenOffset = Number.parseFloat(inner.style.getPropertyValue('--cirrus-row-offset')) || 0;
      const initial = displacement + onScreenOffset;
      rowAnimations.current.get(id)?.stop();
      inner.style.setProperty('--cirrus-row-offset', `${initial}px`);
      const control = animate(initial, 0, {
        ...threadSpring,
        delay: displacement < 0 && prior.top < distanceFromGap && deleted.length ? (delay += .055) - .055 : 0,
        onUpdate: (offset) => inner.style.setProperty('--cirrus-row-offset', `${offset}px`),
        onComplete: () => { inner.style.removeProperty('--cirrus-row-offset'); rowAnimations.current.delete(id); }
      });
      rowAnimations.current.set(id, control);
    }
  }, [key, rows]);

  useEffect(() => {
    const animations = rowAnimations.current;
    return () => { for (const animation of animations.values()) animation.stop(); animations.clear(); };
  }, [key]);

  // Entrance flags are one-shot: clear them so a row re-mounted by the virtualiser does not replay.
  useEffect(() => {
    if (entering.size === 0) return undefined;
    const timer = window.setTimeout(() => setEntering(new Set()), enterMs);
    return () => window.clearTimeout(timer);
  }, [entering]);

  useEffect(() => {
    if (!highlightId) return undefined;
    const timer = window.setTimeout(() => setHighlightId(null), highlightMs);
    return () => window.clearTimeout(timer);
  }, [highlightId]);

  useLayoutEffect(checkLayout, [checkLayout, rows.length]);

  useLayoutEffect(() => {
    if (entering.size === 0 || !sendAnchor?.current || !listRef.current || effectsDisabled()) return;
    const anchor = sendAnchor.current.getBoundingClientRect();
    const arrivals = [...listRef.current.querySelectorAll<HTMLElement>('.message--self[data-entering="true"]')]
      .filter((row) => !row.style.getPropertyValue('--send-y'))
      .map((row) => ({ row, bounds: row.getBoundingClientRect() }));
    for (const { row, bounds } of arrivals) {
      row.style.setProperty('--send-x', `${Math.max(-32, Math.min(32, anchor.right - 22 - bounds.right))}px`);
      row.style.setProperty('--send-y', `${Math.max(12, Math.min(46, anchor.top - bounds.bottom))}px`);
    }
  }, [entering, hasLayout, sendAnchor]);

  const jumpToQuote = useCallback((author: string, text: string) => {
    const target = [...rows].reverse().find((message) => !message.systemMessage
      && (!author || message.displayName === author)
      && (parseQuote(message.content).body.startsWith(text) || message.content.includes(text)));
    if (target && scrollToMessage(target.messageId)) setHighlightId(target.messageId);
  }, [rows, scrollToMessage]);

  const renderRow = (message: MessageItem, index: number) => {
    const position = positions[index];
    const date = dateLabels[index];
    const divider = message.messageId === dividerId
      ? <div key="divider" role="separator" aria-label="以下为新消息" className="unseen-divider my-2 flex items-center justify-center">
          <span className="rounded-full bg-sidebar/80 px-3 py-1 text-small font-medium text-accent-text">以下为新消息</span>
        </div>
      : null;
    return (
      <>
        {date && <div data-testid="date-separator" className="pointer-events-none flex justify-center py-1.5">
          <span className="rounded-full bg-sidebar/90 px-3 py-1 text-small font-medium text-ink shadow-float">{date}</span>
        </div>}
        {divider}
        <MessageBubble message={message} bridge={bridge} grouped={!position.first && !divider && !date} tail={position.last}
          isGroupChat={isGroupChat} onQuote={onQuote} onJumpToQuote={jumpToQuote} canRecall={canRecall?.(message)}
          showTime={settings.showSendTime} animateEntry={entering.has(message.messageId)} highlighted={highlightId === message.messageId} />
      </>
    );
  };

  const loading = rows.length === 0 && (state.connection.phase === 'connecting' || state.connection.phase === 'reconnecting');
  const virtualItems = hasLayout ? virtualizer.getVirtualItems() : [];

  return (
    <div className="cirrus-timeline relative flex min-h-0 flex-1 flex-col">
      <div className="cirrus-sky" aria-hidden="true" />
      <motion.div key={key} initial={{ opacity: 0 }} animate={{ opacity: 1 }} transition={{ duration: scaled(0.18), ease }}
        ref={scrollRef} data-testid="message-timeline" role="log" aria-label="消息时间线" aria-live="polite" onScroll={onScroll}
        className="cirrus-history scroll-thin relative min-h-0 flex-1 overflow-y-auto overflow-x-hidden">
        <div className="cirrus-thread relative flex min-h-full flex-col justify-end pb-3">
          {loading ? <LoadingRows /> : rows.length === 0 ? (
            <EmptyState className="my-auto" icon={<MessageCircle />} title="这里还很安静" description="发送第一条消息开始聊天。" />
          ) : hasLayout ? (
            <div ref={listRef} className="relative w-full" style={{ height: virtualizer.getTotalSize() }}>
              {virtualItems.map((item) => (
                <div key={item.key} data-index={item.index} data-cirrus-row={rows[item.index].messageId} ref={virtualizer.measureElement}
                  className={cn('absolute left-0 top-0 w-full', positions[item.index].first ? 'pt-2' : 'pt-1')}
                  style={{ transform: `translateY(${item.start - virtualizer.options.scrollMargin}px)` }}>
                  <div className="cirrus-row-content">{renderRow(rows[item.index], item.index)}</div>
                </div>
              ))}
            </div>
          ) : (
            <div ref={listRef} className="flex w-full flex-col">
              {rows.map((message, index) => (
                <div key={message.messageId} data-cirrus-row={message.messageId} className={positions[index].first ? 'pt-2' : 'pt-1'}>
                  <div className="cirrus-row-content">{renderRow(message, index)}</div>
                </div>
              ))}
            </div>
          )}
        </div>
      </motion.div>
      <div className="cirrus-exits" ref={exitsRef} aria-hidden="true" />
      <AnimatePresence>
        {!atBottom && rows.length > 0 && (
          <motion.button type="button" onClick={scrollToBottom}
            aria-label={newCount > 0 ? `查看 ${newCount} 条新消息` : '回到底部'} title="回到底部"
            initial={{ opacity: 0, scale: 0.9 }} animate={{ opacity: 1, scale: 1 }} exit={{ opacity: 0, scale: 0.9 }}
            transition={{ duration: 0.2, ease }}
            className="press absolute bottom-4 right-4 z-20 inline-flex size-12 cursor-pointer items-center justify-center rounded-full bg-elevated text-ink-2 shadow-float hover:text-ink">
            <ChevronDown size={24} aria-hidden="true" />
            {newCount > 0 && (
              <span className="num absolute -top-1 left-1/2 inline-flex h-5 min-w-5 -translate-x-1/2 items-center justify-center rounded-full bg-accent-strong px-1 text-small font-medium text-on-accent">
                {newCount > 99 ? '99+' : newCount}
              </span>
            )}
          </motion.button>
        )}
      </AnimatePresence>
    </div>
  );
}

function LoadingRows() {
  return (
    <div aria-label="正在加载消息" className="flex flex-col gap-3 py-3">
      {[60, 40, 72, 36].map((width, index) => (
        <div key={index} className={cn('flex', index % 2 ? 'justify-end' : 'justify-start')}>
          <Skeleton className="h-12 rounded-bubble" style={{ width: `${width}%` }} />
        </div>
      ))}
    </div>
  );
}
