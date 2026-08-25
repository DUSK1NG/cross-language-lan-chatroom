import { createCommand } from '../bridge/chatBridge';
import type { BridgeCommand } from '../bridge/types';

const batchSize = 8;
const maximumInteractiveFrameMs = 1_000;

export type FrameTelemetryReporter = {
  recordFrame(timestampMs: number): void;
  flush(): void;
};

export function createFrameTelemetryReporter(
  dispatch: (command: BridgeCommand) => void
): FrameTelemetryReporter {
  let previousTimestampMs: number | undefined;
  let pendingFrameTimesMs: number[] = [];

  const flush = () => {
    if (pendingFrameTimesMs.length === 0) return;
    dispatch(createCommand('performance.reportFrameTimes', { frameTimesMs: pendingFrameTimesMs }));
    pendingFrameTimesMs = [];
  };

  return {
    recordFrame(timestampMs) {
      if (!Number.isFinite(timestampMs)) return;
      if (previousTimestampMs === undefined) {
        previousTimestampMs = timestampMs;
        return;
      }

      const frameTimeMs = timestampMs - previousTimestampMs;
      previousTimestampMs = timestampMs;
      if (!Number.isFinite(frameTimeMs) || frameTimeMs <= 0 || frameTimeMs > maximumInteractiveFrameMs) return;

      pendingFrameTimesMs.push(frameTimeMs);
      if (pendingFrameTimesMs.length >= batchSize) flush();
    },
    flush
  };
}
