import { act, cleanup, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import type { BridgeState } from '../../bridge/types';
import { ChatHeader } from './ChatHeader';
import { ConnectionBanner } from './ConnectionBanner';

function headerState(overrides: Partial<BridgeState> = {}): BridgeState {
  return {
    schemaVersion: 1,
    connection: { phase: 'connected', statusText: '已连接', retryable: false },
    identity: { displayName: 'Alice', userCode: 'A001', admin: false },
    navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
    rooms: [], directMessages: [], activeMessages: [],
    members: [{ displayName: 'Alice', userCode: 'A001', online: true, admin: false }],
    permissions: { activeRoomCanManage: false },
    savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' },
    ...overrides
  };
}

const header = (state: BridgeState) => (
  <ChatHeader state={state} infoOpen={false} onToggleInfo={() => undefined} searchOpen={false} onSearchOpenChange={() => undefined} onConnectionApprovals={() => undefined} />
);

describe('ChatHeader', () => {
  afterEach(cleanup);

  it('marks mls conversations with the e2ee chip and omits it otherwise', () => {
    const { rerender } = render(header(headerState({
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby', mls: true } }
    })));

    expect(screen.getByText('端到端加密').querySelector('svg[data-icon="e2ee"]')).not.toBeNull();

    rerender(header(headerState()));
    expect(screen.queryByText('端到端加密')).not.toBeInTheDocument();
  });

  it('names the room and counts members and online members', () => {
    render(header(headerState({
      members: [
        { displayName: 'Alice', userCode: 'A001', online: true, admin: false },
        { displayName: 'Bob', userCode: 'B002', online: false, admin: false }
      ]
    })));

    expect(screen.getByRole('heading', { name: '# lobby' })).toBeInTheDocument();
    expect(screen.getByText('2 名成员，1 人在线')).toBeInTheDocument();
  });

  it('shows a direct conversation partner as online or offline from the member list', () => {
    const dm = { page: 'workspace' as const, activeConversation: { kind: 'dm' as const, id: 'B002', title: 'Bob', userCode: 'B002' } };
    const { rerender } = render(header(headerState({ navigation: dm, members: [{ displayName: 'Bob', userCode: 'B002', online: true, admin: false }] })));
    expect(screen.getByText('在线')).toBeInTheDocument();

    rerender(header(headerState({ navigation: dm, members: [] })));
    expect(screen.getByText('离线')).toBeInTheDocument();
  });

  it('shows the pending approval count to administrators and caps it at 99+', () => {
    const approvals = Array.from({ length: 120 }, (_, index) => ({ id: String(index), displayName: `U${index}`, userCode: `U${index}`, requestedAt: '' }));
    const { rerender } = render(header(headerState({ identity: { displayName: 'Alice', userCode: 'A001', admin: true }, connectionApprovals: approvals.slice(0, 2) })));
    expect(screen.getByRole('button', { name: '连接审批' })).toHaveTextContent('2');

    rerender(header(headerState({ identity: { displayName: 'Alice', userCode: 'A001', admin: true }, connectionApprovals: approvals })));
    expect(screen.getByRole('button', { name: '连接审批' })).toHaveTextContent('99+');

    rerender(header(headerState()));
    expect(screen.queryByRole('button', { name: '连接审批' })).not.toBeInTheDocument();
  });
});

describe('ConnectionBanner', () => {
  afterEach(cleanup);

  it('shows the retry counter while reconnecting and stays hidden while connected', () => {
    const { rerender } = render(<ConnectionBanner connection={{ phase: 'connected', statusText: '已连接', retryable: false }} />);
    expect(screen.queryByTestId('connection-banner')).not.toBeInTheDocument();

    rerender(<ConnectionBanner connection={{ phase: 'reconnecting', statusText: '重连中', retryable: true, reconnectAttempt: 3 }} />);
    expect(screen.getByTestId('connection-banner')).toHaveTextContent('重连中 · 第 3 次重连');
  });

  it('confirms the recovery briefly, then disappears', async () => {
    vi.useFakeTimers();
    try {
      const { rerender } = render(<ConnectionBanner connection={{ phase: 'reconnecting', statusText: '重连中', retryable: true }} />);
      rerender(<ConnectionBanner connection={{ phase: 'connected', statusText: '已连接', retryable: false }} />);
      expect(screen.getByTestId('connection-banner')).toHaveTextContent('已重新连接');

      await act(async () => { vi.advanceTimersByTime(3000); });
      expect(screen.queryByText('已重新连接')).not.toBeInTheDocument();
    } finally {
      vi.useRealTimers();
    }
  });

  it('offers reconnecting only once the connection is down', () => {
    const onReconnect = vi.fn();
    const { rerender } = render(<ConnectionBanner connection={{ phase: 'reconnecting', statusText: '重连中', retryable: true }} onReconnect={onReconnect} />);
    expect(screen.queryByRole('button', { name: '重新连接' })).not.toBeInTheDocument();

    rerender(<ConnectionBanner connection={{ phase: 'error', statusText: '连接已断开', retryable: true }} onReconnect={onReconnect} />);
    screen.getByRole('button', { name: '重新连接' }).click();
    expect(onReconnect).toHaveBeenCalledOnce();
  });
});
