import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { createFakeBridge } from '../bridge/chatBridge';
import type { BridgeState } from '../bridge/types';
import { ErrorBoundary } from './ErrorBoundary';

const baseState: BridgeState = {
  schemaVersion: 1,
  connection: { phase: 'idle', statusText: '未连接', retryable: false },
  identity: { displayName: 'Alice', userCode: 'A001', admin: false },
  navigation: { page: 'mode', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
  rooms: [], directMessages: [], activeMessages: [], members: [],
  permissions: { activeRoomCanManage: false },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' }
};

function Boom(): never {
  throw new Error('组件崩溃了');
}

describe('ErrorBoundary', () => {
  afterEach(cleanup);

  it('renders a recoverable error page when a child throws', () => {
    const bridge = createFakeBridge(baseState);
    // 抑制 React 在测试中对未捕获错误打印的噪声。
    const consoleError = vi.spyOn(console, 'error').mockImplementation(() => {});

    render(
      <ErrorBoundary bridge={bridge} onBack={vi.fn()}>
        <Boom />
      </ErrorBoundary>
    );

    expect(screen.getByRole('heading', { name: '界面遇到问题' })).toBeInTheDocument();
    expect(screen.getByRole('alert')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: '重试' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: '返回' })).toBeInTheDocument();

    consoleError.mockRestore();
  });

  it('reports a diagnostics.frontendError command with truncated name and stack', () => {
    const bridge = createFakeBridge(baseState);
    const consoleError = vi.spyOn(console, 'error').mockImplementation(() => {});

    render(
      <ErrorBoundary bridge={bridge} onBack={vi.fn()}>
        <Boom />
      </ErrorBoundary>
    );

    const command = bridge.commands.find((entry) => entry.type === 'diagnostics.frontendError');
    expect(command).toBeDefined();
    expect(typeof command!.payload.name).toBe('string');
    expect(typeof command!.payload.stack).toBe('string');
    expect((command!.payload.name as string).length).toBeLessThanOrEqual(2048);
    expect((command!.payload.stack as string).length).toBeLessThanOrEqual(2048);

    consoleError.mockRestore();
  });

  it('invokes onBack when the 返回 button is clicked', () => {
    const bridge = createFakeBridge(baseState);
    const onBack = vi.fn();
    const consoleError = vi.spyOn(console, 'error').mockImplementation(() => {});

    render(
      <ErrorBoundary bridge={bridge} onBack={onBack}>
        <Boom />
      </ErrorBoundary>
    );

    fireEvent.click(screen.getByRole('button', { name: '返回' }));
    expect(onBack).toHaveBeenCalledOnce();

    consoleError.mockRestore();
  });

  it('renders children unchanged when nothing throws', () => {
    const bridge = createFakeBridge(baseState);
    render(
      <ErrorBoundary bridge={bridge} onBack={vi.fn()}>
        <p>正常内容</p>
      </ErrorBoundary>
    );

    expect(screen.getByText('正常内容')).toBeInTheDocument();
    expect(screen.queryByRole('heading', { name: '界面遇到问题' })).not.toBeInTheDocument();
  });
});
