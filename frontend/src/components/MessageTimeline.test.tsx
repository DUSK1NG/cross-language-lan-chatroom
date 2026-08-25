import { cleanup, fireEvent, render, screen } from '@testing-library/react';
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

function receivedMessage(messageId: string): MessageItemData {
  return { messageId, displayName: 'Bob', userCode: 'B002', time: '10:01', content: messageId, selfMessage: false, systemMessage: false };
}

function setScrollableGeometry(element: HTMLElement, scrollTop: number) {
  Object.defineProperties(element, {
    scrollHeight: { configurable: true, value: 1000 },
    clientHeight: { configurable: true, value: 200 },
    scrollTop: { configurable: true, writable: true, value: scrollTop }
  });
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

  it('pages older history without rendering the entire timeline at once', () => {
    const state = {
      ...baseState,
      activeMessages: Array.from({ length: 600 }, (_, index) => message(`history-${index}`))
    };

    render(<MessageTimeline bridge={createFakeBridge(state)} state={state} />);
    const timeline = screen.getByTestId('message-timeline');
    setScrollableGeometry(timeline, 0);

    expect(screen.getByTestId('message-history-599')).toBeInTheDocument();
    expect(screen.queryByTestId('message-history-499')).toBeNull();

    fireEvent.scroll(timeline);

    expect(screen.getByTestId('message-history-499')).toBeInTheDocument();
    expect(screen.queryByTestId('message-history-599')).toBeNull();
  });

  it('keeps a reader in history and reports how many received messages arrived', () => {
    const firstState = { ...baseState, activeMessages: [receivedMessage('history-1')] };
    const { rerender } = render(<MessageTimeline bridge={createFakeBridge(firstState)} state={firstState} />);
    const timeline = screen.getByTestId('message-timeline');
    setScrollableGeometry(timeline, 0);
    fireEvent.scroll(timeline);

    const nextState = { ...firstState, activeMessages: [...firstState.activeMessages, receivedMessage('new-1')] };
    rerender(<MessageTimeline bridge={createFakeBridge(nextState)} state={nextState} />);

    expect(screen.getByRole('button', { name: '查看 1 条新消息' })).toBeInTheDocument();
    expect(timeline.scrollTop).toBe(0);
  });

  it('jumps to the latest message when the new-message indicator is selected', () => {
    const firstState = { ...baseState, activeMessages: [receivedMessage('history-1')] };
    const { rerender } = render(<MessageTimeline bridge={createFakeBridge(firstState)} state={firstState} />);
    const timeline = screen.getByTestId('message-timeline');
    setScrollableGeometry(timeline, 0);
    fireEvent.scroll(timeline);

    const nextState = { ...firstState, activeMessages: [...firstState.activeMessages, receivedMessage('new-1')] };
    rerender(<MessageTimeline bridge={createFakeBridge(nextState)} state={nextState} />);
    const indicator = document.querySelector<HTMLButtonElement>('.timeline-new-messages');
    expect(indicator).not.toBeNull();
    fireEvent.click(indicator!);

    expect(timeline.scrollTop).toBe(1000);
    expect(document.querySelector('.timeline-new-messages')).toBeNull();
  });

  it('follows the latest message for the sender even after they reviewed history', () => {
    const firstState = { ...baseState, activeMessages: [receivedMessage('history-1')] };
    const { rerender } = render(<MessageTimeline bridge={createFakeBridge(firstState)} state={firstState} />);
    const timeline = screen.getByTestId('message-timeline');
    setScrollableGeometry(timeline, 0);
    fireEvent.scroll(timeline);

    const nextState = { ...firstState, activeMessages: [...firstState.activeMessages, message('self-1')] };
    rerender(<MessageTimeline bridge={createFakeBridge(nextState)} state={nextState} />);

    expect(timeline.scrollTop).toBe(1000);
  });
});
