import type { AttachmentEvent } from './types';

export type AttachmentUploadPhase = 'choosing' | 'uploading' | 'resuming' | 'finalizing' | 'completed' | 'failed';

export type AttachmentUploadState = {
  phase: AttachmentUploadPhase;
  room?: string;
  attachmentId?: string;
  uploadId?: string;
  chunkSize?: number;
  totalChunks?: number;
  logicalSize?: number;
  receivedBytes?: number;
  startedAt?: number;
  lastProgressAt?: number;
  initialReceivedBytes?: number;
  receivedChunks: number;
  receivedIndexes: number[];
  lastChunkIndex: number;
  expiresAt?: string;
  error?: { code: string; message: string };
};

export type AttachmentUploadMap = Record<string, AttachmentUploadState>;

export function beginAttachmentUpload(map: AttachmentUploadMap, commandId: string, room: string): AttachmentUploadMap {
  return { ...map, [commandId]: { phase: 'choosing', receivedChunks: 0, receivedIndexes: [], lastChunkIndex: -1, room } };
}

export function dismissAttachmentUpload(map: AttachmentUploadMap, commandId: string): AttachmentUploadMap {
  const next = { ...map };
  delete next[commandId];
  return next;
}

function withTotalChunks(
  state: AttachmentUploadState,
  chunkSize: number,
  logicalSize: number | undefined
): AttachmentUploadState {
  const totalChunks = logicalSize && logicalSize > 0 ? Math.ceil(logicalSize / chunkSize) : undefined;
  if (totalChunks === undefined) return state;
  const receivedBytes = state.receivedIndexes.reduce((sum, index) =>
    sum + Math.max(0, Math.min(chunkSize, logicalSize! - index * chunkSize)), 0);
  return { ...state, logicalSize, totalChunks, receivedBytes, initialReceivedBytes: receivedBytes,
    phase: state.receivedChunks >= totalChunks ? 'finalizing' : state.phase };
}

export function reduceAttachmentEvent(map: AttachmentUploadMap, event: AttachmentEvent, now = Date.now()): AttachmentUploadMap {
  const current = map[event.id];
  const payload = event.payload;

  if (payload.type === 'attachment.init') {
    const receivedIndexes = [...new Set(payload.receivedIndexes)];
    const baseline: AttachmentUploadState = {
      phase: receivedIndexes.length > 0 ? 'resuming' : 'uploading',
      attachmentId: payload.attachmentId,
      uploadId: payload.uploadId,
      chunkSize: payload.chunkSize,
      receivedChunks: receivedIndexes.length,
      receivedIndexes,
      startedAt: now,
      lastProgressAt: now,
      lastChunkIndex: payload.chunkIndex,
      expiresAt: payload.expiresAt,
      room: map[event.id]?.room
    };
    return { ...map, [event.id]: withTotalChunks(baseline, payload.chunkSize, payload.logicalSize) };
  }

  if (!current || current.phase === 'failed') return map;

  if (payload.type === 'attachment.chunk') {
    if (current.receivedIndexes.includes(payload.chunkIndex)) return map;
    const receivedIndexes = [...current.receivedIndexes, payload.chunkIndex];
    const advanced: AttachmentUploadState = {
      ...current,
      phase: 'uploading',
      receivedChunks: receivedIndexes.length,
      receivedIndexes,
      lastChunkIndex: payload.chunkIndex,
      lastProgressAt: now,
      receivedBytes: (current.receivedBytes ?? 0) + Math.max(0, Math.min(current.chunkSize ?? 0,
        (current.logicalSize ?? Infinity) - payload.chunkIndex * (current.chunkSize ?? 0)))
    };
    return {
      ...map,
      [event.id]: current.totalChunks !== undefined && advanced.receivedChunks >= current.totalChunks
        ? { ...advanced, phase: 'finalizing' }
        : advanced
    };
  }

  if (payload.type === 'attachment.resume') {
    const receivedIndexes = [...new Set(payload.receivedIndexes)];
    const resumed: AttachmentUploadState = {
      ...current,
      phase: 'resuming',
      receivedChunks: receivedIndexes.length,
      receivedIndexes
    };
    return { ...map, [event.id]: withTotalChunks({ ...resumed, startedAt: now, lastProgressAt: now },
      current.chunkSize ?? 0, current.logicalSize) };
  }

  if (payload.type === 'attachment.commit') {
    return { ...map, [event.id]: { ...current, phase: 'completed' } };
  }

  if (payload.type === 'manifest') return map;

  if (payload.type !== 'error') return map;

  return {
    ...map,
    [event.id]: { ...current, phase: 'failed', error: { code: payload.code, message: payload.message } }
  };
}

const attachmentErrorCopyTable: Record<string, string> = {
  ErrAttachmentTooLarge: '文件超过 5 GiB 上限',
  'attachment exceeds the 5 GiB limit': '文件超过 5 GiB 上限',
  ErrRoomQuotaExceeded: '房间附件配额已满（20 GiB）',
  ErrInvalidAttachmentSize: '文件大小无效',
  ErrAttachmentUploadNotFound: '传输异常，请重试',
  ErrAttachmentUploadUnauthorized: '没有在此频道发送附件的权限',
  ErrAttachmentUploadExpired: '上传会话已过期（24 小时），请重新发送',
  ErrAttachmentChunkOutOfRange: '传输异常，请重试',
  ErrAttachmentChunkTooLarge: '传输异常，请重试',
  ErrAttachmentChunkHashMismatch: '分块校验失败，正在自动重传…',
  ErrAttachmentChunkConflict: '传输异常，请重试'
};

export function attachmentErrorCopy(code: string, fallback: string): string {
  return attachmentErrorCopyTable[code] ?? attachmentErrorCopyTable[fallback] ?? (fallback || '附件上传失败');
}
