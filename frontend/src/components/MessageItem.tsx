import { memo, useEffect, useLayoutEffect, useRef, useState } from 'react';

import type { BridgeCommand, ChatBridgeClient, MessageItem as MessageItemData } from '../bridge/types';
import { createCommand } from '../bridge/chatBridge';
import { CommandFeedback } from './CommandFeedback';
import { animateMessage } from '../animation/motion';

type MessageItemProps = {
  message: MessageItemData;
  bridge: ChatBridgeClient;
  onCopy?: (message: MessageItemData) => void;
  onQuote?: (message: MessageItemData) => void;
  onLocalDelete?: (message: MessageItemData) => void;
  onRecall?: (message: MessageItemData) => void;
  canRecall?: boolean;
  showTime?: boolean;
  animateEntry?: boolean;
};

export const MessageItem = memo(function MessageItem({ message, bridge, onCopy, onQuote, onLocalDelete, onRecall, canRecall = false, showTime = true, animateEntry = true }: MessageItemProps) {
  const [feedback, setFeedback] = useState<{ status: 'idle' | 'pending' | 'success' | 'error'; message: string }>({ status: 'idle', message: '' });
  const pendingCommandIdRef = useRef<string | null>(null);
  const pendingUnsubscribeRef = useRef<(() => void) | null>(null);
  const messageRef = useRef<HTMLElement>(null);

  useEffect(() => () => pendingUnsubscribeRef.current?.(), []);
  useLayoutEffect(() => animateEntry ? animateMessage(messageRef.current) : undefined, [message.messageId, animateEntry]);

  const [firstLine = '', ...remainingLines] = message.content.split('\n');
  const isQuotedMessage = firstLine.startsWith('> ') && remainingLines.length > 0;
  const quotedText = isQuotedMessage ? firstLine : '';
  const messageText = isQuotedMessage ? remainingLines.join('\n') : message.content;

  const dispatch = (type: string, label = '操作', payload: Record<string, unknown> = { messageId: message.messageId }) => {
    if (pendingCommandIdRef.current) return;
    const command: BridgeCommand = createCommand(type, payload);
    pendingCommandIdRef.current = command.id;
    setFeedback({ status: 'pending', message: `${label}…` });
    const unsubscribe = bridge.subscribeCommandResult((result) => {
      if (result.id !== command.id || pendingCommandIdRef.current !== command.id) return;
      unsubscribe();
      if (pendingUnsubscribeRef.current === unsubscribe) pendingUnsubscribeRef.current = null;
      pendingCommandIdRef.current = null;
      setFeedback(result.ok
        ? { status: 'success', message: `${label}完成。` }
        : { status: 'error', message: result.error?.message ?? `${label}失败。` });
    });
    pendingUnsubscribeRef.current = unsubscribe;
    bridge.dispatch(command);
  };

  return (
    <article ref={messageRef} className={`message ${message.selfMessage ? 'message--self' : 'message--peer'} ${message.systemMessage ? 'message--system' : ''}`} data-testid={`message-${message.messageId}`}>
      <div className="message-cluster">
        {message.systemMessage ? <p className="message-system-text" data-testid={`message-content-${message.messageId}`}>{message.content}</p> : <>
          <div className="message-meta"><strong>{message.displayName}</strong><span>#{message.userCode}{showTime ? ` · ${message.time}` : ''}</span></div>
          <div className="message-bubble">
            {isQuotedMessage && <blockquote className="message-quote message-content--wrap" data-testid={`message-quote-${message.messageId}`}>{quotedText}</blockquote>}
            {(!isQuotedMessage || messageText) && <p className="message-content message-content--wrap" data-testid={`message-content-${message.messageId}`}>{messageText}</p>}
          </div>
          <div className="message-actions" aria-label={`actions-${message.messageId}`}>
            <button type="button" onClick={() => (onCopy ?? (() => dispatch('message.copy', '已复制', { text: message.content })))(message)}>复制</button>
            <button type="button" onClick={() => (onQuote ?? (() => undefined))(message)}>引用</button>
            {message.selfMessage && <>
              <button type="button" disabled={pendingCommandIdRef.current !== null} onClick={() => (onLocalDelete ?? (() => dispatch('message.removeLocal', '已在本地删除')))(message)}>删除</button>
            </>}
            {canRecall && <>
              <button type="button" disabled={pendingCommandIdRef.current !== null} onClick={() => (onRecall ?? (() => dispatch('message.recall', '撤回')))(message)}>撤回</button>
            </>}
          </div>
          <CommandFeedback {...feedback} />
        </>}
      </div>
    </article>
  );
}, areMessageItemPropsEqual);

function areMessageItemPropsEqual(previous: MessageItemProps, next: MessageItemProps) {
  const previousMessage = previous.message;
  const nextMessage = next.message;
  return previous.bridge === next.bridge
    && previous.onCopy === next.onCopy
    && previous.onQuote === next.onQuote
    && previous.onLocalDelete === next.onLocalDelete
    && previous.onRecall === next.onRecall
    && previous.canRecall === next.canRecall
    && previous.showTime === next.showTime
    && previous.animateEntry === next.animateEntry
    && previousMessage.messageId === nextMessage.messageId
    && previousMessage.displayName === nextMessage.displayName
    && previousMessage.userCode === nextMessage.userCode
    && previousMessage.time === nextMessage.time
    && previousMessage.content === nextMessage.content
    && previousMessage.selfMessage === nextMessage.selfMessage
    && previousMessage.systemMessage === nextMessage.systemMessage;
}
