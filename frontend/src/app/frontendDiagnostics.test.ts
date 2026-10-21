import { describe, expect, it, vi } from 'vitest';

import { createFakeBridge } from '../bridge/chatBridge';
import type { BridgeState } from '../bridge/types';
import {
  FRONTEND_ERROR_COMMAND,
  FRONTEND_ERROR_FIELD_LIMIT,
  registerUnhandledRejectionReporter,
  reportFrontendError,
  truncateUtf8
} from './frontendDiagnostics';

const baseState: BridgeState = {
  schemaVersion: 1,
  connection: { phase: 'idle', statusText: '未连接', retryable: false },
  identity: { displayName: 'Alice', userCode: 'A001', admin: false },
  navigation: { page: 'mode', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
  rooms: [], directMessages: [], activeMessages: [], members: [],
  permissions: { activeRoomCanManage: false },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' }
};

function byteLength(value: string): number {
  return new TextEncoder().encode(value).length;
}

describe('truncateUtf8', () => {
  it('leaves short strings unchanged', () => {
    expect(truncateUtf8('TypeError')).toBe('TypeError');
  });

  it('truncates to at most the byte limit without splitting multibyte chars', () => {
    const long = '错'.repeat(2000); // 每个字符 3 字节，总计 6000 字节
    const result = truncateUtf8(long);
    expect(byteLength(result)).toBeLessThanOrEqual(FRONTEND_ERROR_FIELD_LIMIT);
    expect(result).not.toContain('\uFFFD');
  });
});

describe('reportFrontendError', () => {
  it('dispatches diagnostics.frontendError with truncated fields', () => {
    const bridge = createFakeBridge(baseState);
    reportFrontendError(bridge, 'TypeError', 'x'.repeat(5000));
    const command = bridge.commands.find((entry) => entry.type === FRONTEND_ERROR_COMMAND);
    expect(command).toBeDefined();
    expect(command!.payload.name).toBe('TypeError');
    expect(byteLength(command!.payload.stack as string)).toBeLessThanOrEqual(FRONTEND_ERROR_FIELD_LIMIT);
  });

  it('never throws even if dispatch fails', () => {
    const bridge = { dispatch: vi.fn(() => { throw new Error('bridge down'); }) };
    expect(() => reportFrontendError(bridge, 'Error', 'stack')).not.toThrow();
  });
});

describe('registerUnhandledRejectionReporter', () => {
  it('reports and prevents the default on an unhandled rejection', () => {
    const bridge = createFakeBridge(baseState);
    registerUnhandledRejectionReporter(bridge);

    const error = new Error('后台任务失败');
    const event = new Event('unhandledrejection') as PromiseRejectionEvent & { reason: unknown };
    Object.defineProperty(event, 'reason', { value: error });
    const preventDefault = vi.spyOn(event, 'preventDefault');

    window.dispatchEvent(event);

    const command = bridge.commands.find((entry) => entry.type === FRONTEND_ERROR_COMMAND);
    expect(command).toBeDefined();
    expect(command!.payload.name).toBe('Error');
    expect(preventDefault).toHaveBeenCalledOnce();
  });
});
