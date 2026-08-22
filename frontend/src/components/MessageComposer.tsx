import { useEffect, useLayoutEffect, useRef, useState } from 'react';

import { createCommand } from '../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient, CommandResult } from '../bridge/types';
import { CommandFeedback } from './CommandFeedback';
import { EmojiPicker } from './EmojiPicker';

export type QuoteDraft = { displayName: string; content: string };

export type MessageComposerProps = {
  state: BridgeState;
  bridge: ChatBridgeClient;
  quote?: QuoteDraft | null;
  onClearQuote?(): void;
  draft: string;
  onDraftChange(value: string): void;
  onCommandResult(result: CommandResult): void;
  focusAtEndToken?: number;
};

type Feedback = { status: 'idle' | 'pending' | 'success' | 'error'; message: string };

export function MessageComposer({ state, bridge, quote = null, onClearQuote, draft, onDraftChange, onCommandResult, focusAtEndToken = 0 }: MessageComposerProps) {
  const [feedback, setFeedback] = useState<Feedback>({ status: 'idle', message: '' });
  const [emojiOpen, setEmojiOpen] = useState(false);
  const mountedRef = useRef(true);
  const latestDraftRef = useRef(draft);
  const pendingCommandIdRef = useRef<string | null>(null);
  const pendingUnsubscribeRef = useRef<(() => void) | null>(null);
  const textareaRef = useRef<HTMLTextAreaElement>(null);
  const active = state.navigation.activeConversation;
  latestDraftRef.current = draft;

  useEffect(() => {
    mountedRef.current = true;
    return () => {
      mountedRef.current = false;
      pendingUnsubscribeRef.current?.();
      pendingUnsubscribeRef.current = null;
      pendingCommandIdRef.current = null;
    };
  }, []);

  useLayoutEffect(() => {
    if (focusAtEndToken === 0 || !textareaRef.current) return;
    const textarea = textareaRef.current;
    textarea.focus();
    textarea.setSelectionRange(textarea.value.length, textarea.value.length);
  }, [focusAtEndToken]);

  function submit() {
    const submittedDraft = draft;
    const trimmed = submittedDraft.trim();
    if (!trimmed || !active || pendingCommandIdRef.current) return;
    const submittedContent = quote ? `> ${quote.displayName}: ${quote.content}\n${trimmed}` : trimmed;
    const type = active.kind === 'room' ? 'chat.sendRoom' : 'chat.sendPrivate';
    const payload = active.kind === 'room'
      ? { room: active.id, content: submittedContent }
      : { targetUserCode: active.userCode, content: submittedContent };
    const command = createCommand(type, payload);
    pendingCommandIdRef.current = command.id;
    setFeedback({ status: 'pending', message: '正在发送…' });
    const unsubscribe = bridge.subscribeCommandResult((result) => {
      if (result.id !== command.id || pendingCommandIdRef.current !== command.id) return;
      unsubscribe();
      if (pendingUnsubscribeRef.current === unsubscribe) pendingUnsubscribeRef.current = null;
      pendingCommandIdRef.current = null;
      if (!mountedRef.current) return;
      onCommandResult(result);
      if (result.ok) {
        if (latestDraftRef.current === submittedDraft) onDraftChange('');
        if (quote) onClearQuote?.();
        setFeedback({ status: 'success', message: '消息已发送。' });
        return;
      }
      setFeedback({ status: 'error', message: result.error?.message ?? '消息发送失败。' });
    });
    pendingUnsubscribeRef.current = unsubscribe;
    bridge.dispatch(command);
  }

  function onComposerKeyDown(event: React.KeyboardEvent<HTMLTextAreaElement>) {
    if (event.key !== 'Enter' || event.shiftKey || event.nativeEvent.isComposing) return;
    event.preventDefault();
    submit();
  }

  function insertEmoji(emoji: string) {
    const textarea = textareaRef.current;
    if (!textarea) return;
    const start = textarea.selectionStart ?? draft.length;
    const end = textarea.selectionEnd ?? start;
    onDraftChange(`${draft.slice(0, start)}${emoji}${draft.slice(end)}`);
    requestAnimationFrame(() => {
      textarea.focus();
      const cursor = start + emoji.length;
      textarea.setSelectionRange(cursor, cursor);
    });
  }

  return (
    <div className="composer-area">
      <CommandFeedback {...feedback} />
      {quote && (
        <div className="composer-quote" data-testid="composer-quote">
          <blockquote className="composer-quote__text">&gt; {quote.displayName}: {quote.content}</blockquote>
          {onClearQuote && <button type="button" className="composer-quote__remove" aria-label="移除引用" onClick={onClearQuote}>×</button>}
        </div>
      )}
      <form className="message-composer" onSubmit={(event) => { event.preventDefault(); submit(); }}>
        <textarea ref={textareaRef} aria-label="消息输入框" rows={2} value={draft} onChange={(event) => onDraftChange(event.target.value)} onKeyDown={onComposerKeyDown} placeholder="输入消息…" />
        <button className="emoji-button" type="button" aria-label="表情" onClick={() => setEmojiOpen((open) => !open)}>☺</button>
        <button className="send-button" type="submit" aria-label="发送消息" disabled={!draft.trim() || pendingCommandIdRef.current !== null}>↗</button>
      </form>
      {emojiOpen && <EmojiPicker onSelect={insertEmoji} onClose={() => setEmojiOpen(false)} />}
    </div>
  );
}
