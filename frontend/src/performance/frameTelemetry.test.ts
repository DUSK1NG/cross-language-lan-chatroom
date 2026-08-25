import { describe, expect, it, vi } from 'vitest';

import { createFrameTelemetryReporter } from './frameTelemetry';

describe('frame telemetry reporter', () => {
  it('batches eight browser frame intervals into one bridge command', () => {
    const dispatch = vi.fn();
    const reporter = createFrameTelemetryReporter(dispatch);

    reporter.recordFrame(100);
    for (let index = 1; index <= 8; index += 1) {
      reporter.recordFrame(100 + index * 16);
    }

    expect(dispatch).toHaveBeenCalledTimes(1);
    expect(dispatch).toHaveBeenCalledWith(expect.objectContaining({
      type: 'performance.reportFrameTimes',
      payload: { frameTimesMs: Array(8).fill(16) }
    }));
  });

  it('does not upload invalid or backgrounded frame gaps', () => {
    const dispatch = vi.fn();
    const reporter = createFrameTelemetryReporter(dispatch);

    reporter.recordFrame(100);
    reporter.recordFrame(100);
    reporter.recordFrame(2_500);
    reporter.flush();

    expect(dispatch).not.toHaveBeenCalled();
  });
});
