import type { BridgeState, ChatBridgeClient } from '../bridge/types';
import { MessageItem } from './MessageItem';

type MessageTimelineProps = { state: BridgeState; bridge: ChatBridgeClient };

export function MessageTimeline({ state, bridge }: MessageTimelineProps) {
  const messages = state.activeMessages.length > 500 ? state.activeMessages.slice(-100) : state.activeMessages;
  return (
    <section className="message-timeline" data-testid="message-timeline" aria-label="Message timeline">
      {messages.length === 0 ? (
        <div className="empty-state"><span className="empty-glyph">✦</span><p>No messages yet. Start the conversation.</p></div>
      ) : messages.map((message) => <MessageItem key={message.messageId} message={message} bridge={bridge} />)}
    </section>
  );
}
