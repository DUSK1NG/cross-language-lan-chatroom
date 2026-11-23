import { useCallback, useEffect, useLayoutEffect, useRef, useState, type KeyboardEvent, type RefObject } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { ImagePlus, Paperclip, SendHorizontal, Smile, X } from 'lucide-react';

import { createCommand } from '../../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient, CommandResult } from '../../bridge/types';
import { useAttachmentUploads } from '../../state/useAttachmentUploads';
import { cn } from '../../lib/cn';
import { ease, easeExit, scaled } from '../../lib/motion';
import { CommandFeedback } from '../CommandFeedback';
import { IconButton } from '../ui/icon-button';
import { EmojiPopover } from './EmojiPopover';
import { UploadCard } from './UploadCard';

export type QuoteDraft = { displayName: string; content: string };

export type ComposerProps = {
  state: BridgeState;
  bridge: ChatBridgeClient;
  quote?: QuoteDraft | null;
  onClearQuote?(): void;
  draft: string;
  onDraftChange(value: string): void;
  onCommandResult(result: CommandResult): void;
  focusAtEndToken?: number;
  /** Increment to start an upload (drag-and-drop onto the chat area). */
  uploadRequestToken?: number;
  theme?: 'dark' | 'light';
  sendAnchor?: RefObject<HTMLFormElement | null>;
};

type Feedback = { status: 'idle' | 'pending' | 'success' | 'error'; message: string };
const lineHeightPx = 21;   // 14px × 1.5
const maxLines = 6;

/**
 * Bottom composer (spec §8): attach · auto-growing textarea (1–6 lines) · emoji · send.
 * Enter sends, Shift+Enter breaks the line, Enter during IME composition never sends.
 * The desktop host opens the file picker and sends its contents.
 */
export function Composer({ state, bridge, quote = null, onClearQuote, draft, onDraftChange, onCommandResult, focusAtEndToken = 0, uploadRequestToken = 0, theme = 'dark', sendAnchor }: ComposerProps) {
  const [feedback, setFeedback] = useState<Feedback>({ status: 'idle', message: '' });
  const [emojiOpen, setEmojiOpen] = useState(false);
  const [pending, setPending] = useState(false);
  const { uploads, beginUpload, dismissUpload } = useAttachmentUploads(bridge);
  const mountedRef = useRef(true);
  const latestDraftRef = useRef(draft);
  const pendingIdRef = useRef<string | null>(null);
  const unsubscribeRef = useRef<(() => void) | null>(null);
  const textareaRef = useRef<HTMLTextAreaElement>(null);
  const emojiButtonRef = useRef<HTMLButtonElement>(null);
  const active = state.navigation.activeConversation;
  const canSendImage = state.edition === 'online' && active?.kind === 'dm';
  const fileTarget = state.onlineFiles && active?.kind === 'dm' ? active.userCode : undefined;
  const uploadConversation = active?.kind === 'room' ? active.id : active?.kind === 'dm' ? `dm:${active.userCode}` : undefined;
  latestDraftRef.current = draft;
  const hasText = draft.trim().length > 0;
  const closeEmoji = useCallback((restoreFocus: boolean) => {
    setEmojiOpen(false);
    if (restoreFocus) emojiButtonRef.current?.focus();
  }, []);

  useEffect(() => {
    mountedRef.current = true;
    return () => {
      mountedRef.current = false;
      unsubscribeRef.current?.();
      unsubscribeRef.current = null;
      pendingIdRef.current = null;
    };
  }, []);

  useLayoutEffect(() => {
    if (focusAtEndToken === 0 || !textareaRef.current) return;
    const textarea = textareaRef.current;
    textarea.focus();
    textarea.setSelectionRange(textarea.value.length, textarea.value.length);
  }, [focusAtEndToken]);

  // Auto-grow between one and six lines; beyond that the textarea scrolls internally.
  useLayoutEffect(() => {
    const textarea = textareaRef.current;
    if (!textarea) return;
    textarea.style.height = 'auto';
    const next = Math.min(textarea.scrollHeight, lineHeightPx * maxLines + 16);
    textarea.style.height = next > 0 ? `${next}px` : '';
    textarea.style.overflowY = textarea.scrollHeight > lineHeightPx * maxLines + 16 ? 'auto' : 'hidden';
  }, [draft]);

  function submit() {
    const submittedDraft = draft;
    const trimmed = submittedDraft.trim();
    if (!trimmed || !active || pendingIdRef.current) return;
    const content = quote ? `> ${quote.displayName}: ${quote.content}\n${trimmed}` : trimmed;
    const command = active.kind === 'room'
      ? createCommand('chat.sendRoom', { room: active.id, content })
      : createCommand('chat.sendPrivate', { targetUserCode: active.userCode, content });
    pendingIdRef.current = command.id;
    setPending(true);
    setFeedback({ status: 'pending', message: '正在发送…' });
    const unsubscribe = bridge.subscribeCommandResult((result) => {
      if (result.id !== command.id || pendingIdRef.current !== command.id) return;
      unsubscribe();
      if (unsubscribeRef.current === unsubscribe) unsubscribeRef.current = null;
      pendingIdRef.current = null;
      if (!mountedRef.current) return;
      setPending(false);
      onCommandResult(result);
      if (result.ok) {
        if (latestDraftRef.current === submittedDraft) onDraftChange('');
        if (quote) onClearQuote?.();
        setFeedback({ status: 'idle', message: '' });
        return;
      }
      setFeedback({ status: 'error', message: result.error?.message ?? '消息发送失败。' });
    });
    unsubscribeRef.current = unsubscribe;
    bridge.dispatch(command);
  }

  function onKeyDown(event: KeyboardEvent<HTMLTextAreaElement>) {
    if (event.key === 'Escape' && quote) {
      event.preventDefault();
      event.stopPropagation();
      onClearQuote?.();
      return;
    }
    // keyCode 229 is reported by some IMEs before isComposing is set.
    if (event.key !== 'Enter' || event.shiftKey || event.nativeEvent.isComposing || event.keyCode === 229) return;
    event.preventDefault();
    submit();
  }

  const insertEmoji = useCallback((emoji: string) => {
    const textarea = textareaRef.current;
    const value = latestDraftRef.current;
    const start = textarea?.selectionStart ?? value.length;
    const end = textarea?.selectionEnd ?? start;
    onDraftChange(`${value.slice(0, start)}${emoji}${value.slice(end)}`);
    setEmojiOpen(false);
    requestAnimationFrame(() => {
      if (!textarea) return;
      textarea.focus();
      const cursor = start + emoji.length;
      textarea.setSelectionRange(cursor, cursor);
    });
  }, [onDraftChange]);

  const chooseUpload = useCallback(() => {
    if (!active || pendingIdRef.current) return;
    if (active.kind !== 'room') return;
    const command = createCommand('attachment.chooseUpload', { room: active.id });
    beginUpload(command.id, active.id);
    bridge.dispatch(command);
  }, [active, beginUpload, bridge]);

  const sendFiles = useCallback((paths?: string[]) => {
    if (!fileTarget || paths?.length === 0) return;
    const command = createCommand(paths ? 'onlineFile.dropUpload' : 'onlineFile.chooseUpload', { targetUserCode: fileTarget, ...(paths ? { paths } : {}) });
    beginUpload(command.id, `dm:${fileTarget}`);
    bridge.dispatch(command);
  }, [fileTarget, beginUpload, bridge]);

  useEffect(() => {
    const runtime = window.runtime;
    if (!fileTarget || !runtime?.OnFileDrop) return;
    runtime.OnFileDrop((_x, _y, paths) => sendFiles(paths), true);
    return () => runtime.OnFileDropOff?.();
  }, [fileTarget, sendFiles]);

  const chooseImage = () => {
    if (!active || pendingIdRef.current) return;
    if (active.kind === 'dm' && canSendImage) {
      const command = createCommand('image.chooseUpload', { targetUserCode: active.userCode });
      pendingIdRef.current = command.id;
      setPending(true);
      setFeedback({ status: 'pending', message: '正在发送图片…' });
      const unsubscribe = bridge.subscribeCommandResult((result) => {
        if (result.id !== command.id) return;
        unsubscribe();
        unsubscribeRef.current = null;
        pendingIdRef.current = null;
        setPending(false);
        setFeedback(result.ok ? { status: 'idle', message: '' } : { status: 'error', message: result.error?.message ?? '图片发送失败。' });
      });
      unsubscribeRef.current = unsubscribe;
      bridge.dispatch(command);
      return;
    }
  };

  const firstUploadToken = useRef(uploadRequestToken);
  useEffect(() => {
    if (uploadRequestToken !== firstUploadToken.current) chooseUpload();
    // Only react to a new token, not to chooseUpload's identity.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [uploadRequestToken]);

  function retryUpload(commandId: string) {
    const upload = uploads[commandId];
    if (!upload || upload.room !== uploadConversation) return;
    dismissUpload(commandId);
    if (fileTarget) sendFiles();
    else chooseUpload();
  }

  const visibleUploads = Object.entries(uploads).filter(([, upload]) => upload.phase !== 'completed' && upload.room === uploadConversation);

  return (
    <div className="shrink-0 bg-transparent px-4 pb-4 pt-2">
      <div className="mx-auto flex w-full max-w-[880px] flex-col gap-2">
        {feedback.status === 'error' && <CommandFeedback {...feedback} />}
        <div data-testid="upload-cards" className="flex flex-col gap-2 empty:hidden">
          {visibleUploads.map(([commandId, upload]) => (
            <UploadCard key={commandId} commandId={commandId} upload={upload} onRemove={() => dismissUpload(commandId)} onRetry={() => retryUpload(commandId)} />
          ))}
        </div>
        <div className="composer-surface surface-glass relative rounded-card shadow-float transition-colors duration-[var(--dur-hover)]">
          <AnimatePresence initial={false}>
            {quote && (
              <motion.div data-testid="composer-quote" initial={{ opacity: 0, y: 8 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: 8 }} transition={{ duration: 0.2, ease }}
                className="flex items-center gap-3 border-b border-divider px-4 py-2">
                <span aria-hidden="true" className="h-8 w-[3px] shrink-0 rounded-full bg-accent" />
                <blockquote className="min-w-0 flex-1 text-small">
                  <span className="visually-hidden">&gt; </span>
                  <strong className="block truncate font-semibold text-accent-text">{quote.displayName}</strong>
                  <span className="visually-hidden">: </span>
                  <span className="block truncate text-ink-2">{quote.content}</span>
                </blockquote>
                {onClearQuote && <IconButton size="sm" label="移除引用" tooltip="取消回复（Esc）" onClick={onClearQuote}><X size={16} aria-hidden="true" /></IconButton>}
              </motion.div>
            )}
          </AnimatePresence>
          <form ref={sendAnchor} className="flex items-end gap-1 p-1" onSubmit={(event) => { event.preventDefault(); submit(); }}>
            <IconButton label="添加附件" tooltip="发送附件" disabled={!fileTarget && active?.kind !== 'room'}
              onClick={fileTarget ? () => sendFiles() : chooseUpload}>
              <Paperclip size={20} aria-hidden="true" />
            </IconButton>
            {canSendImage && <IconButton label="发送图片" tooltip="发送图片" disabled={pending} onClick={chooseImage}>
              <ImagePlus size={20} aria-hidden="true" />
            </IconButton>}
            <textarea ref={textareaRef} aria-label="消息输入框" rows={1} value={draft} data-hotkey="composer"
              onChange={(event) => onDraftChange(event.target.value)} onKeyDown={onKeyDown}
              placeholder={active ? (active.kind === 'room' ? `发送到 #${active.title}` : `发消息给 ${active.title}`) : '输入消息…'}
              className="min-h-9 flex-1 resize-none bg-transparent px-2 py-2 text-message leading-[1.5] text-ink outline-none placeholder:text-ink-3 focus-visible:outline-none" />
            <span className="relative">
              <span data-emoji-trigger="">
                <IconButton ref={emojiButtonRef} label="表情" tooltip="表情" aria-expanded={emojiOpen} onClick={() => setEmojiOpen((open) => !open)}>
                  <Smile size={20} aria-hidden="true" />
                </IconButton>
              </span>
              <AnimatePresence>
                {emojiOpen && <EmojiPopover theme={theme} onSelect={insertEmoji} onClose={closeEmoji} />}
              </AnimatePresence>
            </span>
            <AnimatePresence initial={false}>
              {hasText && (
                <motion.span key="send" initial={{ opacity: 0, scale: 0.85 }} animate={{ opacity: 1, scale: 1 }}
                  exit={{ opacity: 0, scale: 0.85, transition: { duration: scaled(0.13), ease: easeExit } }}
                  transition={{ duration: scaled(0.2), ease }}>
                  <button type="submit" aria-label="发送消息" title="发送（Enter）" disabled={pending}
                    className={cn('press inline-flex size-9 cursor-pointer items-center justify-center rounded-full bg-accent-strong text-on-accent disabled:cursor-not-allowed disabled:opacity-50')}>
                    <SendHorizontal size={18} aria-hidden="true" />
                  </button>
                </motion.span>
              )}
            </AnimatePresence>
          </form>
        </div>
      </div>
    </div>
  );
}
