import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import type { BridgeState } from '../bridge/types';
import { ChatHeader } from './ChatHeader';

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

describe('ChatHeader', () => {
  afterEach(cleanup);

  it('shows a busy status dot and the retry counter while reconnecting', () => {
    render(<ChatHeader state={headerState({
      connection: { phase: 'reconnecting', statusText: '重连中', retryable: true, reconnectAttempt: 3 }
    })} onMembers={() => undefined} membersOpen={false} />);

    expect(screen.getByText('重连中 · 第 3 次重连')).toBeInTheDocument();
    expect(document.querySelector('.status-dot--busy')).not.toBeNull();
  });

  it('marks mls conversations with the e2ee chip and omits it otherwise', () => {
    const { rerender } = render(<ChatHeader state={headerState({
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby', mls: true } }
    })} onMembers={() => undefined} membersOpen={false} />);

    expect(screen.getByText('🔒 端到端加密')).toBeInTheDocument();

    rerender(<ChatHeader state={headerState()} onMembers={() => undefined} membersOpen={false} />);
    expect(screen.queryByText('🔒 端到端加密')).not.toBeInTheDocument();
  });
});
