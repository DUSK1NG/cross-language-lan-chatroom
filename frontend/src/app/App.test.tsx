import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import { App } from './App';
import { createFakeBridge } from '../bridge/chatBridge';
import type { BridgeState } from '../bridge/types';

const disconnectedState: BridgeState = {
  schemaVersion: 1,
  connection: { phase: 'idle', statusText: '未连接', retryable: false },
  identity: { displayName: 'Alice', userCode: 'A001', admin: false },
  navigation: { page: 'mode', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
  rooms: [],
  directMessages: [],
  activeMessages: [],
  members: [],
  permissions: { activeRoomCanManage: false },
  savedConnection: {
    serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: ''
  }
};

describe('App', () => {
  afterEach(cleanup);

  it('renders the Chinese mode selection screen while disconnected', () => {
    const bridge = createFakeBridge(disconnectedState);

    render(<App bridge={bridge} />);

    expect(screen.getByRole('heading', { name: '选择聊天方式' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: '远程服务器' })).toBeInTheDocument();
  });

  it('opens the remote connection form from mode selection', () => {
    const bridge = createFakeBridge(disconnectedState);

    render(<App bridge={bridge} />);
    fireEvent.click(screen.getByRole('button', { name: '远程服务器' }));

    expect(screen.getByRole('heading', { name: '连接远程服务器' })).toBeInTheDocument();
    expect(screen.getByLabelText('服务器 IP')).toHaveValue('127.0.0.1');
  });

  it('switches to the workspace when the bridge publishes a connected state', async () => {
    const bridge = createFakeBridge(disconnectedState);
    render(<App bridge={bridge} />);

    bridge.publish({
      ...disconnectedState,
      connection: { phase: 'connected', statusText: '已连接', retryable: false },
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } }
    });

    await waitFor(() => expect(screen.getByRole('heading', { name: '# lobby' })).toBeInTheDocument());
    expect(screen.getByText('Alice #A001')).toBeInTheDocument();
  });
});
