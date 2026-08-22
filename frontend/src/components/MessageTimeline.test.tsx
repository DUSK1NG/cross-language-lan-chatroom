import { cleanup, render } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { createFakeBridge } from '../bridge/chatBridge';
import type { BridgeState, MessageItem as MessageItemData } from '../bridge/types';
import { MessageTimeline } from './MessageTimeline';

const { animateMessage } = vi.hoisted(() => ({ animateMessage: vi.fn(() => () => undefined) }));
vi.mock('../animation/motion', () => ({ animateMessage }));

const baseState: BridgeState = {
  schemaVersion: 1,
  connection: { phase: 'connected', statusText: '已连接', retryable: false },
  identity: { displayName: 'Alice', userCode: 'A001', admin: false },
  navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
  rooms: [],
  directMessages: [],
  activeMessages: [],
  members: [],
  permissions: { activeRoomCanManage: false },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' }
};

function message(messageId: string): MessageItemData {
  return { messageId, displayName: 'Alice', userCode: 'A001', time: '10:01', content: messageId, selfMessage: true, systemMessage: false };
}

describe('MessageTimeline motion budget', () => {
  afterEach(() => {
    cleanup();
    animateMessage.mockClear();
  });

  it('does not animate the initial history snapshot', () => {
    const state = { ...baseState, activeMessages: [message('history-1'), message('history-2')] };

    render(<MessageTimeline bridge={createFakeBridge(state)} state={state} />);

    expect(animateMessage).not.toHaveBeenCalled();
  });

  it('animates only a message appended after the initial snapshot', () => {
    const firstState = { ...baseState, activeMessages: [message('history-1')] };
    const { rerender } = render(<MessageTimeline bridge={createFakeBridge(firstState)} state={firstState} />);
    const nextState = { ...firstState, activeMessages: [...firstState.activeMessages, message('new-1')] };

    rerender(<MessageTimeline bridge={createFakeBridge(nextState)} state={nextState} />);

    expect(animateMessage).toHaveBeenCalledTimes(1);
  });
});
