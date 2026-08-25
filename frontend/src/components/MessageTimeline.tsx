import { useCallback, useLayoutEffect, useRef, useState } from 'react';

import type { BridgeState, ChatBridgeClient, MessageItem as MessageItemData } from '../bridge/types';
import { MessageItem } from './MessageItem';
import { useAppSettings } from '../state/appSettings';

type MessageTimelineProps = {
  state: BridgeState;
  bridge: ChatBridgeClient;
  onQuote?: (message: MessageItemData) => void;
  canRecall?: (message: MessageItemData) => boolean;
};

const bottomThresholdPx = 32;
const historyWindowSize = 100;

function conversationKey(state: BridgeState) {
  const conversation = state.navigation.activeConversation;
  if (!conversation) return '';
  return conversation.kind === 'room' ? `room:${conversation.id}` : `dm:${conversation.userCode}`;
}

export function MessageTimeline({ state, bridge, onQuote, canRecall }: MessageTimelineProps) {
  const settings = useAppSettings();
  const key = conversationKey(state);
  const [windowStart, setWindowStart] = useState(() => Math.max(0, state.activeMessages.length - historyWindowSize));
  const latestWindowStart = Math.max(0, state.activeMessages.length - historyWindowSize);
  const visibleWindowStart = Math.min(windowStart, latestWindowStart);
  const messages = state.activeMessages.slice(visibleWindowStart, visibleWindowStart + historyWindowSize);
  const timelineRef = useRef<HTMLElement>(null);
  const knownMessageIdsRef = useRef<Set<string> | null>(null);
  const activeConversationRef = useRef('');
  const previousMessageCountRef = useRef(state.activeMessages.length);
  const pendingHistoryPageRef = useRef(false);
  const pendingJumpToLatestRef = useRef(false);
  const atBottomRef = useRef(true);
  const [newMessageCount, setNewMessageCount] = useState(0);
  const knownMessageIds = knownMessageIdsRef.current;
  const enteringMessageIds = knownMessageIds
    ? new Set(messages.filter((message) => !knownMessageIds.has(message.messageId)).map((message) => message.messageId))
    : new Set<string>();

  const scrollToLatest = useCallback(() => {
    const timeline = timelineRef.current;
    if (!timeline) return;
    if (visibleWindowStart !== latestWindowStart) {
      pendingJumpToLatestRef.current = true;
      setWindowStart(latestWindowStart);
      return;
    }
    // This direct write is deliberately not a smooth animation. The bridge
    // batches state changes, so each paint follows the latest message without
    // accumulating a scrolling animation backlog during rapid chat traffic.
    timeline.scrollTop = timeline.scrollHeight;
    atBottomRef.current = true;
    setNewMessageCount(0);
  }, [latestWindowStart, visibleWindowStart]);

  const handleScroll = useCallback(() => {
    const timeline = timelineRef.current;
    if (!timeline) return;
    if (timeline.scrollTop <= 0 && visibleWindowStart > 0) {
      pendingHistoryPageRef.current = true;
      setWindowStart(Math.max(0, visibleWindowStart - historyWindowSize));
      return;
    }
    const distance = timeline.scrollHeight - timeline.clientHeight - timeline.scrollTop;
    const atBottom = distance <= bottomThresholdPx;
    atBottomRef.current = atBottom;
    if (atBottom) setNewMessageCount(0);
  }, [visibleWindowStart]);

  useLayoutEffect(() => {
    const previousConversation = activeConversationRef.current;
    const previousIds = knownMessageIdsRef.current;
    const currentIds = new Set(state.activeMessages.map((message) => message.messageId));

    if (previousConversation !== key || previousIds === null) {
      if (previousConversation !== key && visibleWindowStart !== latestWindowStart) {
        setWindowStart(latestWindowStart);
        return;
      }
      activeConversationRef.current = key;
      knownMessageIdsRef.current = currentIds;
      previousMessageCountRef.current = state.activeMessages.length;
      atBottomRef.current = true;
      setNewMessageCount(0);
      scrollToLatest();
      return;
    }

    const previousMessageCount = previousMessageCountRef.current;
    const wasFollowingLatest = visibleWindowStart >= Math.max(0, previousMessageCount - historyWindowSize);
    previousMessageCountRef.current = state.activeMessages.length;
    if (state.activeMessages.length > previousMessageCount && wasFollowingLatest &&
        visibleWindowStart !== latestWindowStart) {
      setWindowStart(latestWindowStart);
      return;
    }

    if (pendingHistoryPageRef.current || pendingJumpToLatestRef.current) {
      const pagedBack = pendingHistoryPageRef.current;
      pendingHistoryPageRef.current = false;
      pendingJumpToLatestRef.current = false;
      knownMessageIdsRef.current = currentIds;
      if (pagedBack) {
        const timeline = timelineRef.current;
        if (timeline) timeline.scrollTop = timeline.scrollHeight;
        atBottomRef.current = true;
        setNewMessageCount(0);
      } else {
        scrollToLatest();
      }
      return;
    }

    const firstNewIndex = state.activeMessages.findIndex((message) => !previousIds.has(message.messageId));
    const appendedMessages = firstNewIndex < 0
      ? []
      : state.activeMessages.slice(firstNewIndex).filter((message) => !previousIds.has(message.messageId));
    const changesOnlyAtTail = firstNewIndex >= 0 && state.activeMessages.slice(firstNewIndex).every((message) => !previousIds.has(message.messageId));
    knownMessageIdsRef.current = currentIds;

    // Prepending older history must not move the reader or count as a new chat.
    if (!changesOnlyAtTail || appendedMessages.length === 0) return;

    const includesOwnMessage = appendedMessages.some((message) => message.userCode === state.identity.userCode);
    if (includesOwnMessage || atBottomRef.current) {
      scrollToLatest();
      return;
    }
    setNewMessageCount((count) => count + appendedMessages.length);
  }, [key, latestWindowStart, messages, scrollToLatest, state.activeMessages, state.identity.userCode, visibleWindowStart]);

  return (
    <div className="message-timeline-shell">
      <section ref={timelineRef} className="message-timeline" data-testid="message-timeline" aria-label="消息时间线" onScroll={handleScroll}>
        {messages.length === 0 ? (
          <div className="empty-state"><span className="empty-glyph">✦</span><p>暂无消息，开始聊天吧。</p></div>
        ) : messages.map((message) => <MessageItem key={message.messageId} message={message} bridge={bridge} onQuote={onQuote} canRecall={canRecall?.(message)} showTime={settings.showSendTime} animateEntry={enteringMessageIds.has(message.messageId)} />)}
      </section>
      {newMessageCount > 0 && <button className="timeline-new-messages" type="button" onClick={scrollToLatest} aria-label={`查看 ${newMessageCount} 条新消息`}>↓ {newMessageCount} 条新消息</button>}
    </div>
  );
}
