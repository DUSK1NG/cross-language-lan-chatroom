import { afterEach, describe, expect, it, vi } from 'vitest';

import { createDesktopBridge } from './chatBridge';
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

afterEach(() => {
  delete window.__TAURI__;
  delete window.runtime;
});

function installDesktop() {
  const dispatch = vi.fn(async (_command: string, _args?: Record<string, unknown>) => undefined);
  const listeners = new Map<string, (event: { payload: unknown }) => void>();
  window.__TAURI__ = {
    core: { invoke: async <T,>(command: string, args?: Record<string, unknown>) =>
      (command === 'chat_state' ? state : await dispatch(command, args)) as T },
    event: { listen: async <T,>(name: string, listener: (event: { payload: T }) => void) => {
      listeners.set(name, (event) => listener({ payload: event.payload as T }));
      return () => { listeners.delete(name); };
    }}
  };
  return { dispatch, emit: (name: string, payload: unknown) => listeners.get(name)?.({ payload }) };
}

describe('createDesktopBridge', () => {
  it('hydrates state and sends the desktop command as JSON', async () => {
    const { dispatch } = installDesktop();
    const bridge = await createDesktopBridge();
    const command = { id: 'web-1', type: 'conversation.selectRoom', payload: { room: 'lobby' } };

    expect(JSON.parse(bridge.currentStateJson())).toEqual(state);
    bridge.dispatch(command);
    expect(dispatch).toHaveBeenCalledWith('chat_dispatch', { json: JSON.stringify(command) });
  });

  it('forwards snapshots without mixing command results and attachment events', async () => {
    const { emit } = installDesktop();
    const bridge = await createDesktopBridge();
    const onState = vi.fn();
    const onResult = vi.fn();
    const onAttachment = vi.fn();
    const onPresence = vi.fn();
    bridge.subscribe(onState);
    bridge.subscribeCommandResult(onResult);
    bridge.subscribeAttachmentEvents(onAttachment);
    bridge.subscribePresence(onPresence);

    const next = { ...state, connection: { ...state.connection, statusText: '已连接' } };
    emit('chat:state', next);
    emit('chat:attachment', { type: 'attachment.event', id: 'web-9', payload: { type: 'attachment.resume', attachmentId: 'att-1', receivedIndexes: [] } });
    emit('chat:result', { id: 'web-1', ok: true });
    emit('chat:presence', 'Bob#B002 joined the chat');

    expect(onState).toHaveBeenCalledWith(next);
    expect(onResult).toHaveBeenCalledExactlyOnceWith({ id: 'web-1', ok: true });
    expect(onAttachment).toHaveBeenCalledExactlyOnceWith(expect.objectContaining({ id: 'web-9' }));
    expect(onPresence).toHaveBeenCalledExactlyOnceWith('Bob#B002 joined the chat');
  });

  it('surfaces a rejected desktop call to the command subscriber', async () => {
    const { dispatch } = installDesktop();
    dispatch.mockRejectedValueOnce(new Error('invalid payload'));
    const bridge = await createDesktopBridge();
    const onResult = vi.fn();
    bridge.subscribeCommandResult(onResult);

    bridge.dispatch({ id: 'web-1', type: 'chat.sendRoom', payload: {} });

    await vi.waitFor(() => expect(onResult).toHaveBeenCalledWith(expect.objectContaining({
      id: 'web-1', ok: false, error: expect.objectContaining({ message: expect.stringContaining('invalid payload') })
    })));
  });
});
