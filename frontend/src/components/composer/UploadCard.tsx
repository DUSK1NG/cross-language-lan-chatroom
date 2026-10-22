import { useEffect, useState } from 'react';
import { AlertCircle, Paperclip, X } from 'lucide-react';

import { attachmentErrorCopy, type AttachmentUploadState } from '../../bridge/attachmentEvents';
import { cn } from '../../lib/cn';
import { Button } from '../ui/button';

function formatTransferBytes(bytes: number): string {
  if (bytes >= 1024 ** 3) return `${(bytes / 1024 ** 3).toFixed(2)} GiB`;
  if (bytes >= 1024 ** 2) return `${(bytes / 1024 ** 2).toFixed(1)} MiB`;
  return `${(bytes / 1024).toFixed(1)} KiB`;
}

/** Upload preview above the composer: ring + bar progress, speed, resume, retry (spec §7). */
export function UploadCard({ commandId, upload, onRemove, onRetry }: { commandId: string; upload: AttachmentUploadState; onRemove(): void; onRetry(): void }) {
  const failed = upload.phase === 'failed';
  const choosing = upload.phase === 'choosing';
  const finalizing = upload.phase === 'finalizing';
  const inFlight = upload.phase === 'uploading' || upload.phase === 'resuming' || finalizing;
  const totalChunks = upload.totalChunks;
  const progress = totalChunks && totalChunks > 0 ? Math.min(1, upload.receivedChunks / totalChunks) : undefined;
  const [now, setNow] = useState(Date.now);
  useEffect(() => {
    if (!inFlight) return undefined;
    const timer = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(timer);
  }, [inFlight]);
  const elapsed = ((upload.lastProgressAt ?? 0) - (upload.startedAt ?? 0)) / 1000;
  const speed = !finalizing && elapsed > 0 && now - (upload.lastProgressAt ?? 0) < 3000
    ? Math.max(0, (upload.receivedBytes ?? 0) - (upload.initialReceivedBytes ?? 0)) / elapsed : 0;
  const percent = progress === undefined ? undefined : `${(progress * 100).toFixed(progress > 0 && progress < 0.01 ? 2 : 1)}%`;
  const ring = 2 * Math.PI * 16;

  return (
    <div data-testid={`attachment-card-${commandId}`}
      className={cn('upload-card flex items-start gap-3 rounded-md border bg-sidebar p-3', failed ? 'upload-card--failed border-danger-text' : 'border-divider')}>
      <span aria-hidden="true" className="relative inline-flex size-10 shrink-0 items-center justify-center text-accent-text">
        {inFlight && progress !== undefined && (
          <svg viewBox="0 0 40 40" className="absolute inset-0 -rotate-90">
            <circle cx="20" cy="20" r="16" fill="none" strokeWidth="3" className="stroke-hover" />
            <circle cx="20" cy="20" r="16" fill="none" strokeWidth="3" strokeLinecap="round" className="stroke-accent"
              strokeDasharray={ring} strokeDashoffset={ring * (1 - progress)} />
          </svg>
        )}
        {failed ? <AlertCircle size={20} className="text-danger-text" /> : <Paperclip size={18} />}
      </span>
      <span className="flex min-w-0 flex-1 flex-col gap-1">
        {failed && <>
          <span className="truncate text-body font-medium text-ink">附件发送失败</span>
          <span className="text-small text-danger-text">{attachmentErrorCopy(upload.error?.code ?? '', upload.error?.message ?? '')}</span>
          <span className="flex gap-2 pt-1">
            <Button size="sm" variant="outline" onClick={onRetry}>重试</Button>
            <Button size="sm" variant="ghost" onClick={onRemove}>移除</Button>
          </span>
        </>}
        {choosing && <>
          <span className="truncate text-body font-medium text-ink">选择中…</span>
          <span className="text-small text-ink-2">请选择要发送的文件</span>
        </>}
        {inFlight && <>
          <span className="flex items-center justify-between gap-3">
            <span className="truncate text-body font-medium text-ink">{finalizing ? '上传完成，正在校验…' : upload.phase === 'resuming' ? '续传中' : '上传中'}</span>
            <strong className="num shrink-0 text-body font-medium text-accent-text">{percent ?? '准备中'}</strong>
          </span>
          {upload.logicalSize !== undefined && (
            <span className="num text-small text-ink-2">
              {formatTransferBytes(upload.receivedBytes ?? 0)} / {formatTransferBytes(upload.logicalSize)}
              {!finalizing && <span> · {formatTransferBytes(speed)}/s</span>}
            </span>
          )}
          <span className={cn('num text-small', upload.phase === 'resuming' ? 'text-warning' : 'text-ink-2')}>
            {upload.phase === 'resuming' ? `连接中断，已保存 ${upload.receivedChunks} 块` : `${upload.receivedChunks} 块`}{totalChunks ? ` / ${totalChunks} 块` : ''} · 端到端加密
          </span>
          <div role="progressbar" aria-label={`附件上传进度 ${commandId}`} aria-valuemin={0} aria-valuemax={totalChunks ?? 0}
            aria-valuenow={upload.receivedChunks} aria-valuetext={percent} className="mt-1 h-1 overflow-hidden rounded-full bg-hover">
            <span className={cn('progress-fill block h-full w-full origin-left rounded-full bg-accent transition-transform duration-[var(--dur-enter)]',
              progress === undefined && 'progress-fill--indeterminate animate-[skeleton-pulse_1.1s_var(--ease)_infinite]')}
              style={progress === undefined ? undefined : { transform: `scaleX(${progress})` }} />
          </div>
        </>}
      </span>
      {!failed && (
        <button type="button" aria-label={choosing ? `取消选择 ${commandId}` : `取消上传 ${commandId}`}
          title={choosing ? '取消' : '隐藏预览（传输协议暂无取消命令）'} onClick={onRemove}
          className="press inline-flex size-8 shrink-0 cursor-pointer items-center justify-center rounded-full text-ink-3 hover:bg-hover hover:text-ink">
          <X size={16} aria-hidden="true" />
        </button>
      )}
    </div>
  );
}
