import { useEffect } from 'react';

import type { ChatBridgeClient } from '../bridge/types';
import { createFrameTelemetryReporter } from './frameTelemetry';

export function useFrameTelemetry(bridge: ChatBridgeClient): void {
  useEffect(() => {
    const reporter = createFrameTelemetryReporter((command) => bridge.dispatch(command));
    let animationFrame = 0;
    const sample = (timestampMs: number) => {
      reporter.recordFrame(timestampMs);
      animationFrame = window.requestAnimationFrame(sample);
    };

    animationFrame = window.requestAnimationFrame(sample);
    return () => {
      window.cancelAnimationFrame(animationFrame);
      reporter.flush();
    };
  }, [bridge]);
}
