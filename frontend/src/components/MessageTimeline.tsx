import { useCallback, useLayoutEffect, useRef, useState } from 'react';

import type { BridgeState, ChatBridgeClient, MessageItem as MessageItemData } from '../bridge/types';
import { MessageItem } from './MessageItem';
import { useAppSettings } from '../state/appSettings';
import { Icon, type IconName } from './Icon';
import { animateStagger } from '../animation/motion';

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
  // First message that arrived while the reader was scrolled up; marks the "新消息" divider.
  const [firstUnseenId, setFirstUnseenId] = useState<string | null>(null);
  const [historyLoading, setHistoryLoading] = useState(false);
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
      setHistoryLoading(true);
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
      setFirstUnseenId(null);
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
        setHistoryLoading(false);
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
    setFirstUnseenId((current) => current ?? appendedMessages[0].messageId);
    setNewMessageCount((count) => count + appendedMessages.length);
  }, [key, latestWindowStart, messages, scrollToLatest, state.activeMessages, state.identity.userCode, visibleWindowStart]);

  return (
    <div className="message-timeline-shell">
      <section ref={timelineRef} className="message-timeline" data-testid="message-timeline" aria-label="消息时间线" onScroll={handleScroll}>
        {messages.length === 0 ? (
          <WelcomeCard conversation={state.navigation.activeConversation} />
        ) : <>
          <div className="date-pill" aria-hidden="true">今天</div>
          {historyLoading && <div className="history-loading" role="status">正在加载更早的消息…</div>}
          {messages.map((message, index) => {
            const previous = index > 0 ? messages[index - 1] : undefined;
            const grouped = !!previous && !previous.systemMessage && !message.systemMessage
              && previous.userCode === message.userCode
              && previous.selfMessage === message.selfMessage;
            const next = index < messages.length - 1 ? messages[index + 1] : undefined;
            const tail = !next || next.systemMessage || message.systemMessage
              || next.userCode !== message.userCode || next.selfMessage !== message.selfMessage;
            const divider = message.messageId === firstUnseenId
              ? <div key={`unseen-${message.messageId}`} className="unseen-divider" role="separator" aria-label="以下为新消息"><span>新消息</span></div>
              : null;
            return [divider, <MessageItem key={message.messageId} message={message} bridge={bridge} onQuote={onQuote} canRecall={canRecall?.(message)} showTime={settings.showSendTime} animateEntry={enteringMessageIds.has(message.messageId)} grouped={grouped && !divider} tail={tail} />];
          })}
        </>}
      </section>
      {newMessageCount > 0 && <button className="timeline-new-messages" type="button" onClick={scrollToLatest} aria-label={`查看 ${newMessageCount} 条新消息`}><Icon name="download" size={14} />{newMessageCount} 条新消息</button>}
    </div>
  );
}

/** 空会话时的欢迎卡片：浮动图标 + 逐个入场的提示小卡。 */
function WelcomeCard({ conversation }: { conversation: BridgeState['navigation']['activeConversation'] }) {
  const ref = useRef<HTMLDivElement>(null);
  const key = conversation ? `${conversation.kind}:${conversation.id}` : '';
  useLayoutEffect(() => animateStagger(ref.current, '.welcome-card__tip', { delay: 0.12, distance: 10 }), [key]);
  const isRoom = conversation?.kind === 'room';
  const title = conversation ? (isRoom ? `欢迎来到 #${conversation.title}` : `和 ${conversation.title} 的私信`) : '这里还很安静';
  const tips: { icon: IconName; title: string; detail: string }[] = [
    { icon: conversation?.mls ? 'lock' : 'shield', title: conversation?.mls ? '端到端加密' : 'TLS 加密传输', detail: conversation?.mls ? '只有会话成员能读取消息与附件' : '消息经 TLS 加密后在局域网内传输' },
    isRoom
      ? { icon: 'attach', title: '发送附件', detail: '文件加密分块上传，断线可续传' }
      : { icon: 'chat', title: '离线也能收到', detail: '对方离线时消息会在上线后送达' },
    { icon: 'smile', title: '引用与表情', detail: '悬停消息即可引用回复，Enter 发送' }
  ];
  return (
    <div ref={ref} className="welcome-card" data-testid="welcome-card">
      <span className="welcome-card__glyph" aria-hidden="true">
        <span className="welcome-card__ring" />
        <Icon name={isRoom ? 'hash' : 'chat'} size={30} />
      </span>
      <strong className="welcome-card__title">{title}</strong>
      <p className="welcome-card__lede">暂无消息，开始聊天吧。</p>
      <div className="welcome-card__tips">
        {tips.map((tip) => (
          <div key={tip.title} className="welcome-card__tip">
            <span className="welcome-card__tip-icon" aria-hidden="true"><Icon name={tip.icon} size={16} /></span>
            <strong>{tip.title}</strong>
            <span>{tip.detail}</span>
          </div>
        ))}
      </div>
    </div>
  );
}
