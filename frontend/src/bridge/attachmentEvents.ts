import type { AttachmentEvent } from './types';

export type AttachmentUploadPhase = 'choosing' | 'uploading' | 'resuming' | 'completed' | 'failed';

export type AttachmentUploadState = {
  phase: AttachmentUploadPhase;
  room?: string;
  attachmentId?: string;
  uploadId?: string;
  chunkSize?: number;
  totalChunks?: number;
  receivedChunks: number;
  lastChunkIndex: number;
  expiresAt?: string;
  error?: { code: string; message: string };
};

export type AttachmentUploadMap = Record<string, AttachmentUploadState>;

export function beginAttachmentUpload(map: AttachmentUploadMap, commandId: string, room: string): AttachmentUploadMap {
  return { ...map, [commandId]: { phase: 'choosing', receivedChunks: 0, lastChunkIndex: -1, room } };
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
  return { ...state, totalChunks, phase: state.receivedChunks >= totalChunks ? 'completed' : state.phase };
}

export function reduceAttachmentEvent(map: AttachmentUploadMap, event: AttachmentEvent): AttachmentUploadMap {
  const current = map[event.id];
  const payload = event.payload;

  if (payload.type === 'attachment.init') {
    const baseline: AttachmentUploadState = {
      phase: payload.receivedIndexes.length > 0 ? 'resuming' : 'uploading',
      attachmentId: payload.attachmentId,
      uploadId: payload.uploadId,
      chunkSize: payload.chunkSize,
      receivedChunks: payload.receivedIndexes.length,
      lastChunkIndex: payload.chunkIndex,
      expiresAt: payload.expiresAt,
      room: map[event.id]?.room
    };
    return { ...map, [event.id]: withTotalChunks(baseline, payload.chunkSize, payload.logicalSize) };
  }

  if (!current || current.phase === 'failed') return map;

  if (payload.type === 'attachment.chunk') {
    const advanced: AttachmentUploadState = {
      ...current,
      phase: 'uploading',
      receivedChunks: current.receivedChunks + 1,
      lastChunkIndex: payload.chunkIndex
    };
    return {
      ...map,
      [event.id]: current.totalChunks !== undefined && advanced.receivedChunks >= current.totalChunks
        ? { ...advanced, phase: 'completed' }
        : advanced
    };
  }

  if (payload.type === 'attachment.resume') {
    const resumed: AttachmentUploadState = {
      ...current,
      phase: 'resuming',
      receivedChunks: payload.receivedIndexes.length
    };
    return {
      ...map,
      [event.id]: current.totalChunks !== undefined && resumed.receivedChunks >= current.totalChunks
        ? { ...resumed, phase: 'completed' }
        : resumed
    };
  }

  return {
    ...map,
    [event.id]: { ...current, phase: 'failed', error: { code: payload.code, message: payload.message } }
  };
}

const attachmentErrorCopyTable: Record<string, string> = {
  ErrAttachmentTooLarge: '文件超过 500 MiB 上限',
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
  return attachmentErrorCopyTable[code] ?? (fallback || '附件上传失败');
}
