import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import { createFakeBridge } from '../bridge/chatBridge';
import type { BridgeState } from '../bridge/types';
import { App } from './App';

const disconnectedState: BridgeState = {
  schemaVersion: 1,
  connection: { phase: 'idle', statusText: '未连接', retryable: false },
  identity: { displayName: 'Alice', userCode: 'A001', admin: false },
  navigation: { page: 'mode', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
  rooms: [], directMessages: [], activeMessages: [], members: [],
  permissions: { activeRoomCanManage: false },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' },
  hostDefaults: {
    serverExe: 'C:/chat/server-go/chat-server.exe',
    certFile: 'C:/chat/certs/server-lan.crt',
    keyFile: 'C:/chat/certs/server-lan.key',
    dbFile: 'C:/chat/server-go/chat.db'
  }
};

describe('App', () => {
  afterEach(cleanup);

  it('renders all connection modes while disconnected', () => {
    render(<App bridge={createFakeBridge(disconnectedState)} />);

    expect(screen.getByRole('button', { name: 'remote-mode' })).toBeEnabled();
    expect(screen.getByRole('button', { name: 'local-host-mode' })).toBeEnabled();
    expect(screen.getByRole('button', { name: 'guest-mode' })).toBeEnabled();
  });

  it('opens the guest form with a guest identity and dispatches a remote connection', () => {
    const bridge = createFakeBridge(disconnectedState);
    render(<App bridge={bridge} />);
    fireEvent.click(screen.getByRole('button', { name: 'guest-mode' }));

    expect(screen.getByDisplayValue('Bob')).toBeInTheDocument();
    expect(screen.getByDisplayValue('B001')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: 'connect-session' }));

    expect(bridge.commands[0]).toMatchObject({
      type: 'session.connectRemote',
      payload: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Bob', userCode: 'B001' }
    });
  });

  it('opens the local host form with bundled paths and dispatches a local-host connection', () => {
    const bridge = createFakeBridge(disconnectedState);
    render(<App bridge={bridge} />);
    fireEvent.click(screen.getByRole('button', { name: 'local-host-mode' }));

    expect(screen.getByDisplayValue('C:/chat/server-go/chat-server.exe')).toBeInTheDocument();
    expect(screen.getByDisplayValue('C:/chat/certs/server-lan.crt')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: 'start-local-host' }));

    expect(bridge.commands[0]).toMatchObject({
      type: 'session.connectLocalHost',
      payload: {
        serverExe: 'C:/chat/server-go/chat-server.exe',
        certFile: 'C:/chat/certs/server-lan.crt',
        keyFile: 'C:/chat/certs/server-lan.key',
        dbFile: 'C:/chat/server-go/chat.db',
        username: 'Alice',
        userCode: 'A001'
      }
    });
  });

  it('auto-fills the private key beside the certificate when the bridge default is empty', () => {
    const bridge = createFakeBridge({
      ...disconnectedState,
      hostDefaults: {
        serverExe: 'C:/chat/server-go/chat-server.exe',
        certFile: 'C:/chat/server-go/certs/server-lan.crt',
        keyFile: '',
        dbFile: 'C:/chat/server-go/chat.db'
      }
    });
    render(<App bridge={bridge} />);
    fireEvent.click(screen.getByRole('button', { name: 'local-host-mode' }));

    expect(screen.getByDisplayValue('C:/chat/server-go/certs/server-lan.key')).toBeInTheDocument();
  });

  it('keeps the local host form visible when bridge defaults are incomplete', () => {
    const bridge = createFakeBridge({ ...disconnectedState, hostDefaults: {} as BridgeState['hostDefaults'] });
    render(<App bridge={bridge} />);
    fireEvent.click(screen.getByRole('button', { name: 'local-host-mode' }));

    expect(screen.getByRole('heading', { name: '创建本地聊天室' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'start-local-host' })).toBeDisabled();
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

    fireEvent.click(screen.getByRole('button', { name: '设置' }));
    expect(screen.getByRole('heading', { name: '设置' })).toBeInTheDocument();
  });

  it('persists the send-time setting from the settings page', async () => {
    localStorage.clear();
    const bridge = createFakeBridge({
      ...disconnectedState,
      connection: { phase: 'connected', statusText: '已连接', retryable: false },
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } }
    });
    render(<App bridge={bridge} />);
    await waitFor(() => expect(screen.getByRole('button', { name: '设置' })).toBeInTheDocument());
    fireEvent.click(screen.getByRole('button', { name: '设置' }));
    fireEvent.click(screen.getByRole('checkbox', { name: '显示发送时间' }));

    expect(localStorage.getItem('lan-chat.showSendTime')).toBe('false');
  });

  it('shows the active Go TLS server and endpoint in settings', async () => {
    const bridge = createFakeBridge({
      ...disconnectedState,
      connection: { phase: 'connected', statusText: '已连接', retryable: false },
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } }
    });
    render(<App bridge={bridge} />);
    await waitFor(() => expect(screen.getByRole('button', { name: '设置' })).toBeInTheDocument());
    fireEvent.click(screen.getByRole('button', { name: '设置' }));

    expect(screen.getByText('Go TLS Server')).toBeInTheDocument();
    expect(screen.getByText('127.0.0.1:8888')).toBeInTheDocument();
  });

  it('shows performance and graphics status and dispatches performance mode changes', async () => {
    const bridge = createFakeBridge({
      ...disconnectedState,
      connection: { phase: 'connected', statusText: '已连接', retryable: false },
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
      performance: {
        mode: 'Balanced', effectiveMode: 'Balanced', effectsEnabled: true,
        animationsEnabled: true, gradientsEnabled: true, animationDurationScale: 0.75,
        observedFrameCount: 24, observedFps: 60, observedP95FrameMs: 16.67,
        observedMaxFrameMs: 22, automaticReason: 'manual-selection'
      },
      graphics: {
        graphicsApi: 'Direct3D 11 / RHI', renderer: 'Unknown', vendor: 'Unknown',
        hardwareAcceleration: true, softwareRendering: false, refreshRate: 60,
        dpi: 96, resolution: '1920 × 1080'
      }
    });
    render(<App bridge={bridge} />);
    await waitFor(() => expect(screen.getByRole('button', { name: '设置' })).toBeInTheDocument());
    fireEvent.click(screen.getByRole('button', { name: '设置' }));

    expect(screen.getByText('Direct3D 11 / RHI')).toBeInTheDocument();
    expect(screen.getByText('60.0 FPS')).toBeInTheDocument();
    fireEvent.change(screen.getByRole('combobox', { name: '性能等级' }), { target: { value: 'Power Saving' } });

    expect(bridge.commands.at(-1)).toMatchObject({
      type: 'settings.setPerformanceMode',
      payload: { mode: 'Power Saving' }
    });
  });
});
