import { memo, useEffect, useLayoutEffect, useRef, useState } from 'react';

import type { BridgeCommand, ChatBridgeClient, MessageAttachment, MessageItem as MessageItemData } from '../bridge/types';
import { createCommand } from '../bridge/chatBridge';
import { CommandFeedback } from './CommandFeedback';
import { animateMessage } from '../animation/motion';

type MessageItemProps = {
  message: MessageItemData;
  bridge: ChatBridgeClient;
  grouped?: boolean;
  onCopy?: (message: MessageItemData) => void;
  onQuote?: (message: MessageItemData) => void;
  onLocalDelete?: (message: MessageItemData) => void;
  onRecall?: (message: MessageItemData) => void;
  canRecall?: boolean;
  showTime?: boolean;
  animateEntry?: boolean;
};

const deliveryTicks: Record<NonNullable<MessageItemData['deliveryState']>, string> = {
  queued: '⏱',
  sent: '✓',
  delivered: '✓✓',
  failed: '⚠'
};

export const MessageItem = memo(function MessageItem({ message, bridge, grouped = false, onCopy, onQuote, onLocalDelete, onRecall, canRecall = false, showTime = true, animateEntry = true }: MessageItemProps) {
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
    <article ref={messageRef} className={`message ${message.selfMessage ? 'message--self' : 'message--peer'}${grouped ? ' message--grouped' : ''}${message.systemMessage ? ' message--system' : ''}`} data-testid={`message-${message.messageId}`}>
      {!message.systemMessage && !message.selfMessage && <span className="message-avatar" aria-hidden="true">{message.displayName.slice(0, 1)}</span>}
      <div className="message-cluster">
        {message.systemMessage ? <p className="message-system-text" data-testid={`message-content-${message.messageId}`}>{message.content}</p> : <>
          {!grouped && <div className="message-who">{message.displayName}</div>}
          <div className="message-bubble">
            {message.attachment && <AttachmentCard attachment={message.attachment} />}
            {isQuotedMessage && <blockquote className="message-quote message-content--wrap" data-testid={`message-quote-${message.messageId}`}>{quotedText}</blockquote>}
            {(!isQuotedMessage || messageText) && <p className="message-content message-content--wrap" data-testid={`message-content-${message.messageId}`}>{messageText}</p>}
            <div className="message-meta">
              {showTime && <span>{message.time}</span>}
              {message.selfMessage && message.deliveryState && <span className={`message-delivery message-delivery--${message.deliveryState}`} aria-label={`投递状态：${message.deliveryState}`}>{deliveryTicks[message.deliveryState]}{message.deliveryState === 'queued' ? ' 发送中' : ''}</span>}
            </div>
          </div>
          <div className="message-actions" aria-label={`actions-${message.messageId}`}>
            <button type="button" onClick={() => (onCopy ?? (() => dispatch('message.copy', '已复制', { text: message.content })))(message)}>复制</button>
            <button type="button" onClick={() => (onQuote ?? (() => undefined))(message)}>引用</button>
            {message.selfMessage && <>
              {message.deliveryState === 'failed' && <button type="button" onClick={() => dispatch('message.retry')}>重试</button>}
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

function AttachmentCard({ attachment }: { attachment: MessageAttachment }) {
  const expired = attachment.status === 'expired';
  const invalid = attachment.status === 'verified-failed';
  const extension = attachment.fileName.includes('.')
    ? attachment.fileName.slice(attachment.fileName.lastIndexOf('.') + 1).toUpperCase().slice(0, 4)
    : 'FILE';
  const sizeMiB = attachment.logicalSize > 0 ? `${(attachment.logicalSize / (1024 * 1024)).toFixed(1)} MiB` : '';
  const statusCopy = expired
    ? '文件已过期（超过 24 小时），请让发送者重新发送'
    : invalid
      ? '校验失败，等待发送者重传'
      : attachment.status === 'downloading'
        ? `接收中… ${attachment.receivedChunks ?? 0}${attachment.totalChunks ? ` / ${attachment.totalChunks} 块` : ''}`
        : '';
  return (
    <div className={`message-attachment${expired ? ' message-attachment--expired' : ''}${invalid ? ' message-attachment--invalid' : ''}`} data-testid={`message-attachment-${attachment.attachmentId}`}>
      <span className="message-attachment__badge" aria-hidden="true">{extension}</span>
      <span className="message-attachment__copy">
        <strong className="message-attachment__name">{attachment.fileName}</strong>
        <span className="message-attachment__meta">
          {sizeMiB && <span>{sizeMiB}</span>}
          <span>🔒 端到端加密</span>
          {statusCopy && <span>{statusCopy}</span>}
        </span>
      </span>
      <button className="message-attachment__download" type="button" aria-label={`下载 ${attachment.fileName}`}
        disabled
        title="下载通道联调中（待对齐）">↓</button>
    </div>
  );
}

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
    && previous.grouped === next.grouped
    && previousMessage.messageId === nextMessage.messageId
    && previousMessage.displayName === nextMessage.displayName
    && previousMessage.userCode === nextMessage.userCode
    && previousMessage.time === nextMessage.time
    && previousMessage.content === nextMessage.content
    && previousMessage.selfMessage === nextMessage.selfMessage
    && previousMessage.systemMessage === nextMessage.systemMessage
    && previousMessage.deliveryState === nextMessage.deliveryState
    && previousMessage.attachment === nextMessage.attachment;
}
