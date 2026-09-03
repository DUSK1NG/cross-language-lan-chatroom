import { useEffect, useLayoutEffect, useRef, useState } from 'react';

import { createCommand } from '../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient, CommandResult } from '../bridge/types';
import { attachmentErrorCopy, type AttachmentUploadState } from '../bridge/attachmentEvents';
import { useAttachmentUploads } from '../state/useAttachmentUploads';
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
  const { uploads, beginUpload, dismissUpload } = useAttachmentUploads(bridge);
  const mountedRef = useRef(true);
  const latestDraftRef = useRef(draft);
  const pendingCommandIdRef = useRef<string | null>(null);
  const pendingUnsubscribeRef = useRef<(() => void) | null>(null);
  const textareaRef = useRef<HTMLTextAreaElement>(null);
  const active = state.navigation.activeConversation;
  const activeRoomId = active?.kind === 'room' ? active.id : undefined;
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

  // 契约（用户定稿）：只发 chooseUpload，文件选择/加密/分块全部由 C++ 完成；
  // UI 不接触路径、明文、密钥。允许多张卡并发，一卡对应一个命令 id。
  function chooseUpload() {
    if (!active || active.kind !== 'room') return;
    const command = createCommand('attachment.chooseUpload', { room: active.id });
    beginUpload(command.id, active.id);
    bridge.dispatch(command);
  }

  function retryUpload(commandId: string) {
    const upload = uploads[commandId];
    if (!upload || !active || active.kind !== 'room' || upload.room !== active.id) return;
    dismissUpload(commandId);
    chooseUpload();
  }

  return (
    <div className="composer-area">
      <CommandFeedback {...feedback} />
      <div className="upload-cards" data-testid="upload-cards">
        {Object.entries(uploads)
          .filter(([, upload]) => upload.phase !== 'completed' && upload.room === activeRoomId)
          .map(([commandId, upload]) => (
            <UploadCard key={commandId} commandId={commandId} upload={upload}
              onRemove={() => dismissUpload(commandId)} onRetry={() => retryUpload(commandId)} />
          ))}
      </div>
      {quote && (
        <div className="composer-quote" data-testid="composer-quote">
          <blockquote className="composer-quote__text">&gt; {quote.displayName}: {quote.content}</blockquote>
          {onClearQuote && <button type="button" className="composer-quote__remove" aria-label="移除引用" onClick={onClearQuote}>×</button>}
        </div>
      )}
      <form className="message-composer" onSubmit={(event) => { event.preventDefault(); submit(); }}>
        <textarea ref={textareaRef} aria-label="消息输入框" rows={2} value={draft} onChange={(event) => onDraftChange(event.target.value)} onKeyDown={onComposerKeyDown} placeholder="输入消息…" />
        <button className="attach-button" type="button" aria-label="添加附件"
          title={active?.kind === 'room' ? '发送附件' : '附件暂仅支持频道会话'}
          disabled={!active || active.kind !== 'room'}
          onClick={chooseUpload}>📎</button>
        <button className="emoji-button" type="button" aria-label="表情" onClick={() => setEmojiOpen((open) => !open)}>☺</button>
        <button className="send-button" type="submit" aria-label="发送消息" disabled={!draft.trim() || pendingCommandIdRef.current !== null}>↗</button>
      </form>
      {emojiOpen && <EmojiPicker onSelect={insertEmoji} onClose={() => setEmojiOpen(false)} />}
    </div>
  );
}

function UploadCard({ commandId, upload, onRemove, onRetry }: {
  commandId: string;
  upload: AttachmentUploadState;
  onRemove(): void;
  onRetry(): void;
}) {
  const failed = upload.phase === 'failed';
  const choosing = upload.phase === 'choosing';
  const inFlight = upload.phase === 'uploading' || upload.phase === 'resuming';
  const totalChunks = upload.totalChunks;
  const progress = totalChunks && totalChunks > 0 ? Math.min(1, upload.receivedChunks / totalChunks) : undefined;
  return (
    <div className={`upload-card${failed ? ' upload-card--failed' : ''}`} data-testid={`attachment-card-${commandId}`}>
      <span className="upload-card__badge" aria-hidden="true">📎</span>
      <span className="upload-card__body">
        {failed && <>
          <span className="upload-card__title">附件发送失败</span>
          <span className="upload-card__meta upload-card__meta--error">{attachmentErrorCopy(upload.error?.code ?? '', upload.error?.message ?? '')}</span>
          <span className="upload-card__actions">
            <button type="button" onClick={onRetry}>重试</button>
            <button type="button" onClick={onRemove}>移除</button>
          </span>
        </>}
        {choosing && <>
          <span className="upload-card__title">选择中…</span>
          <span className="upload-card__meta">正在等待系统文件对话框；文件不会离开本机，之后按 47 KiB 分块加密上传</span>
        </>}
        {inFlight && <>
          <span className="upload-card__title">{upload.attachmentId ? `${upload.attachmentId.slice(0, 8)} · ` : ''}{upload.phase === 'resuming' ? '续传中' : '上传中'}</span>
          <span className={`upload-card__meta${upload.phase === 'resuming' ? ' upload-card__meta--warn' : ''}`}>
            {upload.phase === 'resuming' ? `⚡ 连接中断，已保存 ${upload.receivedChunks} 块` : `${upload.receivedChunks} 块`}{totalChunks ? ` / ${totalChunks} 块` : ''} · 🔒 端到端加密
          </span>
          <div className="progress-track" role="progressbar" aria-label={`附件上传进度 ${commandId}`}
            aria-valuemin={0} aria-valuemax={totalChunks ?? 0} aria-valuenow={upload.receivedChunks}>
            <span className={`progress-fill${progress === undefined ? ' progress-fill--indeterminate' : ''}`}
              style={progress === undefined ? undefined : { transform: `scaleX(${progress})` }} />
          </div>
        </>}
      </span>
      {!failed && <button className="upload-card__remove" type="button"
        aria-label={choosing ? `取消选择 ${commandId}` : `取消上传 ${commandId}`}
        title={choosing ? undefined : '取消命令待对齐：仅隐藏预览卡'}
        onClick={onRemove}>✕</button>}
    </div>
  );
}
