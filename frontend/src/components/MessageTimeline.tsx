import { useLayoutEffect, useRef } from 'react';

import type { BridgeState, ChatBridgeClient, MessageItem as MessageItemData } from '../bridge/types';
import { MessageItem } from './MessageItem';
import { useAppSettings } from '../state/appSettings';

type MessageTimelineProps = {
  state: BridgeState;
  bridge: ChatBridgeClient;
  onQuote?: (message: MessageItemData) => void;
  canRecall?: (message: MessageItemData) => boolean;
};

export function MessageTimeline({ state, bridge, onQuote, canRecall }: MessageTimelineProps) {
  const settings = useAppSettings();
  const messages = state.activeMessages.length > 500 ? state.activeMessages.slice(-100) : state.activeMessages;
  const knownMessageIdsRef = useRef<Set<string> | null>(null);
  const knownMessageIds = knownMessageIdsRef.current;
  const enteringMessageIds = knownMessageIds
    ? new Set(messages.filter((message) => !knownMessageIds.has(message.messageId)).map((message) => message.messageId))
    : new Set<string>();

  useLayoutEffect(() => {
    knownMessageIdsRef.current = new Set(messages.map((message) => message.messageId));
  }, [messages]);

  return (
    <section className="message-timeline" data-testid="message-timeline" aria-label="消息时间线">
      {messages.length === 0 ? (
        <div className="empty-state"><span className="empty-glyph">✦</span><p>暂无消息，开始聊天吧。</p></div>
      ) : messages.map((message) => <MessageItem key={message.messageId} message={message} bridge={bridge} onQuote={onQuote} canRecall={canRecall?.(message)} showTime={settings.showSendTime} animateEntry={enteringMessageIds.has(message.messageId)} />)}
    </section>
  );
}
