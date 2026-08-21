import { describe, expect, it, vi } from 'vitest';

import { createQtBridge, createWebChannelBridge } from './chatBridge';
import type { BridgeState } from './types';

const state: BridgeState = {
  schemaVersion: 1,
  connection: { phase: 'connected', statusText: 'Connected', retryable: false },
  identity: { displayName: 'Alice', userCode: 'A001', admin: false },
  navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
  rooms: [], directMessages: [], activeMessages: [], members: [],
  permissions: { activeRoomCanManage: false },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' }
};

describe('createQtBridge', () => {
  it('hydrates from currentStateJson and serializes dispatch commands', async () => {
    const proxy = {
      currentStateJson: vi.fn((callback: (json: string) => void) => callback(JSON.stringify(state))),
      dispatch: vi.fn(),
      stateChanged: { connect: vi.fn() },
      commandResult: { connect: vi.fn() },
      bridgeError: { connect: vi.fn() }
    };

    const bridge = await createQtBridge(proxy);
    const command = { id: 'web-1', type: 'conversation.selectRoom', payload: { room: 'lobby' } };

    expect(JSON.parse(bridge.currentStateJson())).toEqual(state);
    bridge.dispatch(command);
    expect(proxy.dispatch).toHaveBeenCalledWith(JSON.stringify(command));
  });

  it('forwards stateChanged snapshots to subscribers', async () => {
    let stateListener: ((json: string) => void) | undefined;
    const proxy = {
      currentStateJson: (callback: (json: string) => void) => callback(JSON.stringify(state)),
      dispatch: vi.fn(),
      stateChanged: { connect: (listener: (json: string) => void) => { stateListener = listener; } },
      commandResult: { connect: vi.fn() },
      bridgeError: { connect: vi.fn() }
    };
    const bridge = await createQtBridge(proxy);
    const listener = vi.fn();
    bridge.subscribe(listener);

    stateListener?.(JSON.stringify({ ...state, connection: { ...state.connection, statusText: 'Updated' } }));

    expect(listener).toHaveBeenCalledWith(expect.objectContaining({ connection: expect.objectContaining({ statusText: 'Updated' }) }));
  });

  it('discovers the chatBridge object through the Qt transport', async () => {
    const proxy = {
      currentStateJson: (callback: (json: string) => void) => callback(JSON.stringify(state)),
      dispatch: vi.fn(),
      stateChanged: { connect: vi.fn() },
      commandResult: { connect: vi.fn() },
      bridgeError: { connect: vi.fn() }
    };
    class FakeWebChannel {
      constructor(_transport: unknown, callback: (channel: { objects: Record<string, typeof proxy> }) => void) {
        callback({ objects: { chatBridge: proxy } });
      }
    }
    window.qt = { webChannelTransport: {} };
    window.QWebChannel = FakeWebChannel;

    const bridge = await createWebChannelBridge();

    expect(JSON.parse(bridge.currentStateJson())).toEqual(state);
    delete window.qt;
    delete window.QWebChannel;
  });

  it('rejects a channel that does not expose chatBridge', async () => {
    class EmptyWebChannel {
      constructor(_transport: unknown, callback: (channel: { objects: Record<string, never> }) => void) {
        callback({ objects: {} });
      }
    }
    window.qt = { webChannelTransport: {} };
    window.QWebChannel = EmptyWebChannel;

    await expect(createWebChannelBridge()).rejects.toThrow('chatBridge object is unavailable');
    delete window.qt;
    delete window.QWebChannel;
  });

  it('forwards command results from the Qt proxy', async () => {
    let resultListener: ((json: string) => void) | undefined;
    const proxy = {
      currentStateJson: (callback: (json: string) => void) => callback(JSON.stringify(state)),
      dispatch: vi.fn(),
      stateChanged: { connect: vi.fn() },
      commandResult: { connect: (listener: (json: string) => void) => { resultListener = listener; } },
      bridgeError: { connect: vi.fn() }
    };
    const bridge = await createQtBridge(proxy);
    const listener = vi.fn();
    bridge.subscribeCommandResult(listener);

    resultListener?.('{"id":"web-1","ok":false,"error":{"code":"rejected","message":"no","retryable":false,"source":"controller"}}');

    expect(listener).toHaveBeenCalledWith(expect.objectContaining({ id: 'web-1', ok: false }));
  });
});
