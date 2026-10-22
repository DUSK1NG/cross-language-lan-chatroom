import { memo, useEffect, useRef, useState, type ReactNode } from 'react';
import { motion } from 'framer-motion';
import { AlertCircle, Check, Clock3, Copy, CornerUpLeft, MoreHorizontal, RotateCcw, Trash2, Undo2 } from 'lucide-react';

import type { BridgeCommand, ChatBridgeClient, MessageItem as MessageItemData } from '../../bridge/types';
import { createCommand } from '../../bridge/chatBridge';
import { avatarTone } from '../../lib/avatarColor';
import { cn } from '../../lib/cn';
import { ease } from '../../lib/motion';
import { Avatar } from '../ui/avatar';
import { ContextMenu, DropdownMenu, type MenuItem } from '../ui/menu';
import { toast } from '../ui/toast';
import { CommandFeedback } from '../CommandFeedback';
import { FileCard } from './FileCard';
import { RichText } from './RichText';

type Feedback = { status: 'idle' | 'pending' | 'success' | 'error'; message: string };

export type MessageBubbleProps = {
  message: MessageItemData;
  bridge: ChatBridgeClient;
  /** Not the first message of its group: hides the sender line. */
  grouped?: boolean;
  /** Last message of its group: carries the tail corner and the avatar. */
  tail?: boolean;
  /** Group chats show sender name and avatar for others; DMs do not. */
  isGroupChat?: boolean;
  onQuote?: (message: MessageItemData) => void;
  onCopy?: (message: MessageItemData) => void;
  onLocalDelete?: (message: MessageItemData) => void;
  onRecall?: (message: MessageItemData) => void;
  onJumpToQuote?: (author: string, text: string) => void;
  canRecall?: boolean;
  showTime?: boolean;
  /** Plays the 200ms rise-in (new arrivals only, never history). */
  animateEntry?: boolean;
  highlighted?: boolean;
};

const deliveryCopy: Record<NonNullable<MessageItemData['deliveryState']>, string> = {
  queued: '发送中',
  sent: '已发送',
  delivered: '已送达',
  failed: '发送失败，点击重试'
};

/** Splits the stored "> 作者: 原文\n回复" form (protocol unchanged) into quote and reply. */
export function parseQuote(content: string) {
  const [firstLine = '', ...rest] = content.split('\n');
  if (!firstLine.startsWith('> ') || rest.length === 0) return { quote: null, body: content };
  const match = /^> ([^:：]{1,40})[:：] ?(.*)$/.exec(firstLine);
  return { quote: { raw: firstLine, author: match?.[1] ?? '', text: match ? match[2] : firstLine.slice(2) }, body: rest.join('\n') };
}

export const MessageBubble = memo(function MessageBubble({
  message, bridge, grouped = false, tail = true, isGroupChat = true, onQuote, onCopy, onLocalDelete, onRecall, onJumpToQuote,
  canRecall = false, showTime = true, animateEntry = false, highlighted = false
}: MessageBubbleProps) {
  const [feedback, setFeedback] = useState<Feedback>({ status: 'idle', message: '' });
  const pendingRef = useRef<string | null>(null);
  const unsubscribeRef = useRef<(() => void) | null>(null);
  useEffect(() => () => unsubscribeRef.current?.(), []);

  const self = message.selfMessage;
  const tone = avatarTone(message.userCode || message.displayName);

  const run = (type: string, label: string, payload: Record<string, unknown> = { messageId: message.messageId }) => {
    if (pendingRef.current) return;
    const command: BridgeCommand = createCommand(type, payload);
    pendingRef.current = command.id;
    setFeedback({ status: 'pending', message: `${label}…` });
    const unsubscribe = bridge.subscribeCommandResult((result) => {
      if (result.id !== command.id || pendingRef.current !== command.id) return;
      unsubscribe();
      if (unsubscribeRef.current === unsubscribe) unsubscribeRef.current = null;
      pendingRef.current = null;
      setFeedback(result.ok ? { status: 'success', message: `${label}完成。` } : { status: 'error', message: result.error?.message ?? `${label}失败。` });
    });
    unsubscribeRef.current = unsubscribe;
    bridge.dispatch(command);
  };

  if (message.systemMessage) {
    return (
      <article data-testid={`message-${message.messageId}`} className="message message--system flex justify-center py-1">
        <p data-testid={`message-content-${message.messageId}`}
          className="message-system-text rounded-full bg-sidebar/80 px-3 py-1 text-center text-small text-ink-2">{message.content}</p>
      </article>
    );
  }

  const { quote, body } = parseQuote(message.content);
  const copy = () => (onCopy ?? (() => { bridge.dispatch(createCommand('message.copy', { text: message.content })); toast.success('已复制'); }))(message);
  const reply = () => onQuote?.(message);
  const remove = () => (onLocalDelete ?? (() => run('message.removeLocal', '已在本地删除')))(message);
  const recall = () => (onRecall ?? (() => run('message.recall', '撤回')))(message);
  const retry = () => run('message.retry', '重新发送');
  const busy = pendingRef.current !== null;

  const menu: MenuItem[] = [
    { label: '回复', icon: <CornerUpLeft />, onSelect: reply },
    { label: '复制', icon: <Copy />, onSelect: copy },
    ...(self && message.deliveryState === 'failed' ? [{ label: '重新发送', icon: <RotateCcw />, onSelect: retry }] : []),
    ...(canRecall ? [{ label: '撤回', icon: <Undo2 />, onSelect: recall, disabled: busy }] : []),
    ...(self ? [{ label: '删除', icon: <Trash2 />, onSelect: remove, danger: true, disabled: busy }] : [])
  ];

  const showSender = isGroupChat && !self && !grouped;
  const showAvatar = isGroupChat && !self;
  const metaClass = self ? 'text-bubble-out-meta' : 'text-bubble-in-meta';

  return (
    <motion.article data-testid={`message-${message.messageId}`} data-entering={animateEntry ? 'true' : undefined}
      initial={animateEntry ? { opacity: 0, y: 8 } : false} animate={{ opacity: 1, y: 0 }}
      transition={{ duration: 0.2, ease }}
      className={cn('message group/message relative flex w-full items-end gap-2',
        self ? 'message--self justify-end' : 'message--peer justify-start',
        grouped && 'message--grouped', tail && 'message--tail')}>
      {showAvatar && (
        <span className="w-9 shrink-0 self-end">{tail ? <Avatar name={message.displayName} seed={message.userCode} size={36} /> : null}</span>
      )}
      <div className={cn('relative flex min-w-0 max-w-[65%] flex-col', self ? 'items-end' : 'items-start')}>
        <ContextMenu items={menu}>
          <div className={cn('message-bubble relative flow-root min-w-0 max-w-full px-3 py-2 text-message leading-[1.5] text-ink transition-colors duration-[var(--dur-panel)]',
            self ? 'bg-bubble-out' : 'bg-bubble-in',
            'rounded-bubble',
            tail && (self ? 'rounded-br-tail' : 'rounded-bl-tail'),
            highlighted && 'ring-2 ring-accent')}>
            {showSender && (
              <p className="message-sender mb-1 truncate text-small font-semibold" style={{ color: `var(--name-${tone})` }}>{message.displayName}</p>
            )}
            {message.attachment && <div className="mb-1"><FileCard attachment={message.attachment} bridge={bridge} self={self} /></div>}
            {quote && (
              <blockquote data-testid={`message-quote-${message.messageId}`}
                className={cn('message-quote mb-1 cursor-pointer rounded-sm border-l-[3px] py-1 pl-2 pr-2 text-small', self ? 'border-read-tick bg-quote-out-bg' : 'border-accent bg-quote-in-bg')}
                onClick={() => onJumpToQuote?.(quote.author, quote.text)}>
                {quote.author
                  ? <><span className="visually-hidden">&gt; </span><strong className={cn('block truncate font-semibold', self ? 'text-quote-out-name' : 'text-accent-text')}>{quote.author}</strong><span className="visually-hidden">: </span><span className={cn('block truncate', self ? 'text-quote-out-text' : 'text-bubble-in-meta')}>{quote.text}</span></>
                  : <span className={cn('block truncate', self ? 'text-quote-out-text' : 'text-bubble-in-meta')}>{quote.raw}</span>}
              </blockquote>
            )}
            {(!quote || body) && (
              <p data-testid={`message-content-${message.messageId}`} className="message-content message-content--wrap inline whitespace-pre-wrap break-words [overflow-wrap:anywhere] select-text">
                <RichText text={body} self={self} onCopyPath={(path) => { bridge.dispatch(createCommand('message.copy', { text: path })); toast.success('路径已复制'); }} />
              </p>
            )}
            {/* Time and status float to the bottom-right of the last line: inline with short text, wrapped below long text (spec §6). */}
            <span className={cn('message-meta num relative top-1 float-right ml-2 inline-flex h-4 items-center gap-1 whitespace-nowrap text-small leading-none', metaClass)}>
              {showTime && <span>{message.time}</span>}
              {self && message.deliveryState && <DeliveryMark state={message.deliveryState} onRetry={retry} />}
            </span>
          </div>
        </ContextMenu>
        <MessageActions id={message.messageId} self={self} onReply={reply} onCopy={copy} menu={menu.slice(2)} />
        <CommandFeedback {...feedback} />
      </div>
    </motion.article>
  );
}, areEqual);

function DeliveryMark({ state, onRetry }: { state: NonNullable<MessageItemData['deliveryState']>; onRetry: () => void }) {
  const label = deliveryCopy[state];
  const className = cn('message-delivery inline-flex items-center', `message-delivery--${state}`);
  if (state === 'failed') {
    return (
      <span aria-label="投递状态：failed" title={label} className={className}>
        <button type="button" onClick={onRetry} aria-label="重试发送" className="press inline-flex cursor-pointer text-danger-icon-out">
          <AlertCircle size={16} aria-hidden="true" />
        </button>
        <span className="visually-hidden">发送失败</span>
      </span>
    );
  }
  const icon: ReactNode = state === 'queued' ? <Clock3 size={14} aria-hidden="true" />
    : state === 'delivered' ? <><Check size={14} aria-hidden="true" className="-mr-2" /><Check size={14} aria-hidden="true" /></>
      : <Check size={14} aria-hidden="true" />;
  return (
    <span aria-label={`投递状态：${state}`} title={label} className={className}>
      {icon}
      {state === 'queued' && <span className="visually-hidden">发送中</span>}
    </span>
  );
}

/** Hover bar outside the bubble (spec §6). Hidden until hover/focus; not shown on touch-only devices. */
function MessageActions({ id, self, onReply, onCopy, menu }: { id: string; self: boolean; onReply: () => void; onCopy: () => void; menu: MenuItem[] }) {
  const button = 'press inline-flex size-8 cursor-pointer items-center justify-center rounded-full text-ink-2 hover:bg-hover hover:text-ink';
  return (
    <div aria-label={`actions-${id}`}
      className={cn('message-actions absolute top-0 z-10 flex items-center gap-1 rounded-full bg-elevated p-1 shadow-float',
        'opacity-0 pointer-events-none transition-opacity duration-[var(--dur-hover)] group-hover/message:opacity-100 group-hover/message:pointer-events-auto focus-within:opacity-100 focus-within:pointer-events-auto',
        '[@media(hover:none)]:hidden',
        self ? 'right-full mr-2' : 'left-full ml-2')}>
      <button type="button" aria-label="回复" title="回复" onClick={onReply} className={button}><CornerUpLeft size={16} aria-hidden="true" /></button>
      <button type="button" aria-label="复制" title="复制" onClick={onCopy} className={button}><Copy size={16} aria-hidden="true" /></button>
      {menu.length > 0 && (
        <DropdownMenu align="end" items={menu}
          trigger={<button type="button" aria-label="更多操作" title="更多" className={button}><MoreHorizontal size={16} aria-hidden="true" /></button>} />
      )}
    </div>
  );
}

function areEqual(previous: MessageBubbleProps, next: MessageBubbleProps) {
  const a = previous.message;
  const b = next.message;
  return previous.bridge === next.bridge && previous.grouped === next.grouped && previous.tail === next.tail
    && previous.isGroupChat === next.isGroupChat && previous.canRecall === next.canRecall && previous.showTime === next.showTime
    && previous.animateEntry === next.animateEntry && previous.highlighted === next.highlighted
    && previous.onQuote === next.onQuote && previous.onCopy === next.onCopy && previous.onLocalDelete === next.onLocalDelete
    && previous.onRecall === next.onRecall && previous.onJumpToQuote === next.onJumpToQuote
    && a.messageId === b.messageId && a.content === b.content && a.time === b.time && a.displayName === b.displayName
    && a.userCode === b.userCode && a.selfMessage === b.selfMessage && a.systemMessage === b.systemMessage
    && a.deliveryState === b.deliveryState && a.attachment === b.attachment;
}

