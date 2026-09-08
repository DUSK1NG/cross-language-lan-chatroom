import { describe, expect, it } from 'vitest';

import type { AttachmentEvent } from './types';
import {
  attachmentErrorCopy,
  beginAttachmentUpload,
  dismissAttachmentUpload,
  reduceAttachmentEvent
} from './attachmentEvents';

function initEvent(id: string, receivedIndexes: number[] = [], logicalSize?: number): AttachmentEvent {
  return {
    type: 'attachment.event',
    id,
    payload: {
      type: 'attachment.init', attachmentId: 'att-1234567890abcdef', uploadId: 'up-1',
      chunkSize: 48128, chunkIndex: 0, receivedIndexes,
      expiresAt: '2026-09-03T00:00:00Z', content: '',
      ...(logicalSize !== undefined ? { logicalSize } : {})
    }
  };
}

function chunkEvent(id: string, chunkIndex: number): AttachmentEvent {
  return {
    type: 'attachment.event', id,
    payload: { type: 'attachment.chunk', attachmentId: 'att-1234567890abcdef', chunkIndex, content: '' }
  };
}

describe('beginAttachmentUpload', () => {
  it('registers a choosing card keyed by the chooseUpload command id', () => {
    const next = beginAttachmentUpload({}, 'web-1', 'lobby');
    expect(next['web-1']).toMatchObject({ phase: 'choosing', receivedChunks: 0, lastChunkIndex: -1 });
  });
});

describe('reduceAttachmentEvent', () => {
  it('turns an init with an empty baseline into an uploading card', () => {
    const next = reduceAttachmentEvent({}, initEvent('web-1'));
    expect(next['web-1']).toMatchObject({
      phase: 'uploading', attachmentId: 'att-1234567890abcdef', uploadId: 'up-1',
      chunkSize: 48128, receivedChunks: 0, lastChunkIndex: 0, expiresAt: '2026-09-03T00:00:00Z'
    });
    expect(next['web-1'].totalChunks).toBeUndefined();
  });

  it('derives totalChunks from the native plaintext logicalSize', () => {
    const next = reduceAttachmentEvent({}, initEvent('web-1', [], 48128 * 3));
    expect(next['web-1'].totalChunks).toBe(3);
  });

  it('counts a partial final chunk and keeps five of six chunks in progress', () => {
    let map = reduceAttachmentEvent({}, initEvent('web-1', [], 48128 * 5 + 17));
    for (let index = 0; index < 5; index++) map = reduceAttachmentEvent(map, chunkEvent('web-1', index));
    expect(map['web-1']).toMatchObject({ phase: 'uploading', receivedChunks: 5, totalChunks: 6 });
    expect(map['web-1'].receivedChunks / map['web-1'].totalChunks!).toBeCloseTo(5 / 6);
  });

  it('counts repeated and out-of-order chunk acks only once', () => {
    let map = reduceAttachmentEvent({}, initEvent('web-1', [], 48128 * 3));
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 2));
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 0));
    const beforeDuplicate = map;
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 2));
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 0));
    expect(map).toBe(beforeDuplicate);
    expect(map['web-1']).toMatchObject({ phase: 'uploading', receivedChunks: 2, totalChunks: 3 });
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 1));
    expect(map['web-1']).toMatchObject({ phase: 'finalizing', receivedChunks: 3 });
    expect(reduceAttachmentEvent(map, chunkEvent('web-1', 1))).toBe(map);
  });

  it('deduplicates init baselines and ignores already acknowledged indexes', () => {
    let map = reduceAttachmentEvent({}, initEvent('web-1', [0, 0, 2], 48128 * 4));
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 2));
    expect(map['web-1']).toMatchObject({ receivedChunks: 2, totalChunks: 4 });
  });

  it('replaces the ack index baseline on resume and does not double count replayed acks', () => {
    let map = reduceAttachmentEvent({}, initEvent('web-1', [], 48128 * 4));
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 3));
    map = reduceAttachmentEvent(map, {
      type: 'attachment.event', id: 'web-1',
      payload: { type: 'attachment.resume', attachmentId: 'att-1234567890abcdef', receivedIndexes: [0, 0, 2] }
    });
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 2));
    expect(map['web-1']).toMatchObject({ phase: 'resuming', receivedChunks: 2, totalChunks: 4 });
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 3));
    expect(map['web-1']).toMatchObject({ phase: 'uploading', receivedChunks: 3, totalChunks: 4 });
  });

  it('turns an init with a non-empty baseline into a resuming card', () => {
    const next = reduceAttachmentEvent({}, initEvent('web-1', [0, 1, 2]));
    expect(next['web-1']).toMatchObject({ phase: 'resuming', receivedChunks: 3 });
  });

  it('counts chunk acks per command id', () => {
    let map = reduceAttachmentEvent({}, initEvent('web-1'));
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 1));
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 2));
    expect(map['web-1']).toMatchObject({ phase: 'uploading', receivedChunks: 2, lastChunkIndex: 2 });
  });

  it('ignores chunk acks for unknown command ids', () => {
    const next = reduceAttachmentEvent({}, chunkEvent('web-unknown', 1));
    expect(next).toEqual({});
  });

  it('waits for server commit after every derived chunk is acknowledged', () => {
    let map = reduceAttachmentEvent({}, initEvent('web-1', [], 48128 * 2));
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 0));
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 1));
    expect(map['web-1'].phase).toBe('finalizing');
    map = reduceAttachmentEvent(map, { type: 'attachment.event', id: 'web-1',
      payload: { type: 'attachment.commit', attachmentId: 'att-1', content: 'committed' } });
    expect(map['web-1'].phase).toBe('completed');
  });

  it('tracks actual bytes including the short final chunk and ignores duplicate progress', () => {
    let map = reduceAttachmentEvent({}, initEvent('bytes', [], 48128 + 17), 1000);
    map = reduceAttachmentEvent(map, chunkEvent('bytes', 1), 2000);
    expect(map.bytes).toMatchObject({ receivedBytes: 17, startedAt: 1000, lastProgressAt: 2000 });
    map = reduceAttachmentEvent(map, chunkEvent('bytes', 0), 3000);
    expect(map.bytes).toMatchObject({ receivedBytes: 48145, lastProgressAt: 3000 });
    expect(reduceAttachmentEvent(map, chunkEvent('bytes', 0), 4000)).toBe(map);
  });

  it('marks a resume event as resuming with the reported baseline', () => {
    let map = reduceAttachmentEvent({}, initEvent('web-1'));
    map = reduceAttachmentEvent(map, {
      type: 'attachment.event', id: 'web-1',
      payload: { type: 'attachment.resume', attachmentId: 'att-1234567890abcdef', receivedIndexes: [0, 1] }
    });
    expect(map['web-1']).toMatchObject({ phase: 'resuming', receivedChunks: 2 });
  });

  it('marks an error payload as failed and keeps the raw code', () => {
    let map = reduceAttachmentEvent({}, initEvent('web-1'));
    map = reduceAttachmentEvent(map, {
      type: 'attachment.event', id: 'web-1',
      payload: { type: 'error', code: 'ErrRoomQuotaExceeded', message: 'quota' }
    });
    expect(map['web-1']).toMatchObject({
      phase: 'failed',
      error: { code: 'ErrRoomQuotaExceeded', message: 'quota' }
    });
  });

  it('does not resurrect a failed card when late chunk acks arrive', () => {
    let map = reduceAttachmentEvent({}, initEvent('web-1'));
    map = reduceAttachmentEvent(map, {
      type: 'attachment.event', id: 'web-1',
      payload: { type: 'error', code: 'ErrAttachmentChunkHashMismatch', message: 'hash' }
    });
    map = reduceAttachmentEvent(map, chunkEvent('web-1', 5));
    expect(map['web-1'].phase).toBe('failed');
  });

  it('preserves the room assigned at begin when attachment.init arrives', () => {
    let map = beginAttachmentUpload({}, 'web-1', 'lobby');
    map = reduceAttachmentEvent(map, initEvent('web-1'));
    expect(map['web-1'].room).toBe('lobby');
  });
});

describe('dismissAttachmentUpload', () => {
  it('removes the card without touching other concurrent uploads', () => {
    let map = beginAttachmentUpload({}, 'web-1', 'lobby');
    map = beginAttachmentUpload(map, 'web-2', 'general');
    map = dismissAttachmentUpload(map, 'web-1');
    expect(Object.keys(map)).toEqual(['web-2']);
  });
});

describe('attachmentErrorCopy', () => {
  it('maps the ten server sentinels to user copy', () => {
    expect(attachmentErrorCopy('ErrAttachmentTooLarge', '')).toBe('文件超过 5 GiB 上限');
    expect(attachmentErrorCopy('', 'attachment exceeds the 5 GiB limit')).toBe('文件超过 5 GiB 上限');
    expect(attachmentErrorCopy('ErrRoomQuotaExceeded', '')).toBe('房间附件配额已满（20 GiB）');
    expect(attachmentErrorCopy('ErrInvalidAttachmentSize', '')).toBe('文件大小无效');
    expect(attachmentErrorCopy('ErrAttachmentUploadNotFound', '')).toBe('传输异常，请重试');
    expect(attachmentErrorCopy('ErrAttachmentUploadUnauthorized', '')).toBe('没有在此频道发送附件的权限');
    expect(attachmentErrorCopy('ErrAttachmentUploadExpired', '')).toBe('上传会话已过期（24 小时），请重新发送');
    expect(attachmentErrorCopy('ErrAttachmentChunkOutOfRange', '')).toBe('传输异常，请重试');
    expect(attachmentErrorCopy('ErrAttachmentChunkTooLarge', '')).toBe('传输异常，请重试');
    expect(attachmentErrorCopy('ErrAttachmentChunkHashMismatch', '')).toBe('分块校验失败，正在自动重传…');
    expect(attachmentErrorCopy('ErrAttachmentChunkConflict', '')).toBe('传输异常，请重试');
  });

  it('falls back to the bridge message for unaligned codes', () => {
    expect(attachmentErrorCopy('ErrSomethingNew', 'bridge said no')).toBe('bridge said no');
    expect(attachmentErrorCopy('ErrSomethingNew', '')).toBe('附件上传失败');
  });
});
