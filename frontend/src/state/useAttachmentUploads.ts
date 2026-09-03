import { useCallback, useEffect, useState } from 'react';

import {
  beginAttachmentUpload,
  dismissAttachmentUpload,
  reduceAttachmentEvent,
  type AttachmentUploadMap
} from '../bridge/attachmentEvents';
import type { AttachmentEvent, ChatBridgeClient } from '../bridge/types';

// Codex C++ 的 chunk ack 以 `<commandId>-chunk-<N>` 作为信封 id（gui_connection_worker.cpp:576），
// 上传卡以 chooseUpload 的原始命令 id 为键；归一化以便两种 id 命中同一张卡。
function normalizeChunkEnvelope(event: AttachmentEvent): AttachmentEvent {
  const suffix = /-chunk-\d+$/.exec(event.id)?.[0];
  return suffix ? { ...event, id: event.id.slice(0, event.id.length - suffix.length) } : event;
}

export function useAttachmentUploads(bridge: ChatBridgeClient): {
  uploads: AttachmentUploadMap;
  beginUpload(commandId: string, room: string): void;
  dismissUpload(commandId: string): void;
} {
  const [uploads, setUploads] = useState<AttachmentUploadMap>({});

  useEffect(() => bridge.subscribeAttachmentEvents((event) => {
    setUploads((current) => reduceAttachmentEvent(current, normalizeChunkEnvelope(event)));
  }), [bridge]);

  const beginUpload = useCallback((commandId: string, room: string) => {
    setUploads((current) => beginAttachmentUpload(current, commandId, room));
  }, []);

  const dismissUpload = useCallback((commandId: string) => {
    setUploads((current) => dismissAttachmentUpload(current, commandId));
  }, []);

  return { uploads, beginUpload, dismissUpload };
}
