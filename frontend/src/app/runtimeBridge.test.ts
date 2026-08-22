import { describe, expect, it } from 'vitest';

import { resolveRuntimeBridge } from './runtimeBridge';

const fakeBridge = { name: 'fake-bridge' } as never;

describe('resolveRuntimeBridge', () => {
  it('does not hide a missing QWebChannel behind FakeBridge in production', async () => {
    const result = await resolveRuntimeBridge({
      hasTransport: false,
      isDevelopment: false,
      createBridge: async () => fakeBridge,
      createPreviewBridge: () => fakeBridge
    });

    expect(result.bridge).toBeUndefined();
    expect(result.error).toContain('QWebChannel');
  });

  it('does not fall back to FakeBridge when the production bridge fails', async () => {
    const result = await resolveRuntimeBridge({
      hasTransport: true,
      isDevelopment: false,
      createBridge: async () => { throw new Error('bridge unavailable'); },
      createPreviewBridge: () => fakeBridge
    });

    expect(result.bridge).toBeUndefined();
    expect(result.error).toContain('bridge unavailable');
  });

  it('keeps FakeBridge only for an explicit development preview', async () => {
    const result = await resolveRuntimeBridge({
      hasTransport: false,
      isDevelopment: true,
      createBridge: async () => fakeBridge,
      createPreviewBridge: () => fakeBridge
    });

    expect(result.bridge).toBe(fakeBridge);
    expect(result.error).toBeUndefined();
  });
});
