import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import { createFakeBridge } from '../bridge/chatBridge';
import type { BridgeState } from '../bridge/types';
import { App } from './App';

const connectedWorkspace: BridgeState = {
  schemaVersion: 1,
  connection: { phase: 'connected', statusText: '已连接', retryable: false },
  identity: { displayName: 'Alice', userCode: 'A001', admin: false },
  navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
  rooms: [{ roomName: 'lobby', memberCount: 1, unreadCount: 0 }],
  directMessages: [],
  activeMessages: [],
  members: [{ displayName: 'Alice', userCode: 'A001', online: true, admin: false }],
  permissions: { activeRoomCanManage: false },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' }
};

describe('connection recovery', () => {
  afterEach(cleanup);

  it('keeps the active workspace and draft through reconnecting until reconnected', async () => {
    const bridge = createFakeBridge(connectedWorkspace);
    render(<App bridge={bridge} />);

    const composer = screen.getByRole('textbox');
    fireEvent.change(composer, { target: { value: 'draft that must survive' } });

    bridge.publish({
      ...connectedWorkspace,
      connection: { phase: 'reconnecting', statusText: '正在重新连接（第 1 次）', retryable: true }
    });

    await waitFor(() => expect(screen.getByRole('heading', { name: '# lobby' })).toBeInTheDocument());
    expect(screen.getByRole('textbox')).toHaveValue('draft that must survive');
    expect(screen.getByText('正在重新连接（第 1 次）')).toBeInTheDocument();

    bridge.publish(connectedWorkspace);

    await waitFor(() => expect(screen.getByText('已连接')).toBeInTheDocument());
    expect(screen.getByRole('textbox')).toHaveValue('draft that must survive');
  });
});
