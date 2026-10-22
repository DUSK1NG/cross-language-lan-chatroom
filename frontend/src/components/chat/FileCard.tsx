import { Download, FileArchive, FileAudio, FileCode2, FileImage, FileText, FileVideo, File as FileIcon, Lock } from 'lucide-react';

import { createCommand } from '../../bridge/chatBridge';
import type { ChatBridgeClient, MessageAttachment } from '../../bridge/types';
import { cn } from '../../lib/cn';

const typeIcons: [RegExp, typeof FileIcon][] = [
  [/\.(png|jpe?g|gif|webp|bmp|svg)$/i, FileImage],
  [/\.(mp4|webm|mov|mkv|avi)$/i, FileVideo],
  [/\.(mp3|wav|flac|ogg|m4a)$/i, FileAudio],
  [/\.(zip|7z|rar|tar|gz|xz)$/i, FileArchive],
  [/\.(ts|tsx|js|go|cpp|hpp|py|json|ps1|sh)$/i, FileCode2],
  [/\.(txt|md|pdf|docx?|xlsx?|pptx?|csv)$/i, FileText]
];

function iconFor(name: string) {
  return typeIcons.find(([pattern]) => pattern.test(name))?.[1] ?? FileIcon;
}

export function formatSize(bytes: number): string {
  if (bytes >= 1024 ** 3) return `${(bytes / 1024 ** 3).toFixed(1)} GiB`;
  if (bytes >= 1024 ** 2) return `${(bytes / 1024 ** 2).toFixed(1)} MiB`;
  if (bytes >= 1024) return `${(bytes / 1024).toFixed(1)} KiB`;
  return `${bytes} B`;
}

/**
 * File attachment card (spec §7): type icon · single-line name · size · download.
 * Image thumbnails need the media cache from spec 03, so images render as files for now.
 */
export function FileCard({ attachment, bridge, self }: { attachment: MessageAttachment; bridge: ChatBridgeClient; self: boolean }) {
  const expired = attachment.status === 'expired';
  const invalid = attachment.status === 'verified-failed';
  const downloading = attachment.status === 'downloading';
  const Icon = iconFor(attachment.fileName);
  const status = expired ? '文件已过期（超过 24 小时），请让发送者重新发送'
    : invalid ? '校验失败，等待发送者重传'
      : downloading ? `接收中… ${attachment.receivedChunks ?? 0}${attachment.totalChunks ? ` / ${attachment.totalChunks} 块` : ''}` : '';
  const meta = self ? 'text-bubble-out-meta' : 'text-bubble-in-meta';
  return (
    <div data-testid={`message-attachment-${attachment.attachmentId}`}
      className={cn('message-attachment flex min-w-[240px] max-w-[320px] items-center gap-3 rounded-md p-2',
        self ? 'bg-on-accent/10' : 'bg-hover', expired && 'message-attachment--expired', invalid && 'message-attachment--invalid')}>
      <span aria-hidden="true" className={cn('inline-flex size-12 shrink-0 items-center justify-center rounded-md',
        invalid ? 'bg-danger-strong text-on-accent' : expired ? 'bg-muted-badge text-on-accent' : 'bg-accent-strong text-on-accent')}>
        <Icon size={24} />
      </span>
      <span className="flex min-w-0 flex-1 flex-col">
        <span className="truncate text-body font-medium" title={attachment.fileName}>{attachment.fileName}</span>
        <span className={cn('num flex flex-wrap items-center gap-x-2 text-small', meta)}>
          {attachment.logicalSize > 0 && <span>{formatSize(attachment.logicalSize)}</span>}
          <span className="inline-flex items-center gap-1"><Lock size={12} aria-hidden="true" />端到端加密</span>
          {status && <span className={cn(invalid && (self ? 'text-on-accent' : 'text-danger-text'))}>{status}</span>}
        </span>
      </span>
      <button type="button" aria-label={`下载 ${attachment.fileName}`} title={expired || invalid ? status : '下载附件'}
        disabled={expired || invalid}
        onClick={() => bridge.dispatch(createCommand('attachment.download', { attachmentId: attachment.attachmentId, chunkIndex: 0 }))}
        className={cn('press inline-flex size-9 shrink-0 cursor-pointer items-center justify-center rounded-full disabled:cursor-not-allowed disabled:opacity-50',
          self ? 'text-on-accent hover:bg-on-accent/10' : 'text-accent-text hover:bg-active/40')}>
        <Download size={20} aria-hidden="true" />
      </button>
    </div>
  );
}
