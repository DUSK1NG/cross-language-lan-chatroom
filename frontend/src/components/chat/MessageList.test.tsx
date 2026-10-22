import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { createFakeBridge } from '../../bridge/chatBridge';
import type { BridgeState, MessageItem as MessageItemData } from '../../bridge/types';
import { MessageList } from './MessageList';

const baseState: BridgeState = {
  schemaVersion: 1,
  connection: { phase: 'connected', statusText: '已连接', retryable: false },
  identity: { displayName: 'Alice', userCode: 'A001', admin: false },
  navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
  rooms: [], directMessages: [], activeMessages: [], members: [],
  permissions: { activeRoomCanManage: false },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' }
};

function message(messageId: string): MessageItemData {
  return { messageId, displayName: 'Alice', userCode: 'A001', time: '10:01', content: messageId, selfMessage: true, systemMessage: false };
}

function receivedMessage(messageId: string, time = '10:01'): MessageItemData {
  return { messageId, displayName: 'Bob', userCode: 'B002', time, content: messageId, selfMessage: false, systemMessage: false };
}

function withMessages(activeMessages: MessageItemData[]): BridgeState {
  return { ...baseState, activeMessages };
}

/** jsdom has no layout: give the scroller a viewport so it behaves like a real, scrollable list. */
function setScrollableGeometry(element: HTMLElement, scrollTop: number) {
  Object.defineProperties(element, {
    scrollHeight: { configurable: true, value: 1000 },
    clientHeight: { configurable: true, value: 200 },
    scrollTop: { configurable: true, writable: true, value: scrollTop }
  });
}

/** Renders a short conversation and scrolls the reader up into history. */
function renderScrolledUp(first: MessageItemData[]) {
  const bridge = createFakeBridge(withMessages(first));
  const view = render(<MessageList bridge={bridge} state={withMessages(first)} />);
  const timeline = screen.getByTestId('message-timeline');
  setScrollableGeometry(timeline, 0);
  fireEvent.scroll(timeline);
  return { ...view, bridge, timeline };
}

describe('MessageList', () => {
  afterEach(cleanup);

  it('does not animate the initial history snapshot', () => {
    render(<MessageList bridge={createFakeBridge(baseState)} state={withMessages([message('history-1'), message('history-2')])} />);

    expect(document.querySelector('[data-entering]')).toBeNull();
  });

  it('animates only a message appended after the initial snapshot', () => {
    const first = withMessages([message('history-1')]);
    const bridge = createFakeBridge(first);
    const { rerender } = render(<MessageList bridge={bridge} state={first} />);

    rerender(<MessageList bridge={bridge} state={withMessages([message('history-1'), message('new-1')])} />);

    expect(document.querySelectorAll('[data-entering]')).toHaveLength(1);
    expect(screen.getByTestId('message-new-1')).toHaveAttribute('data-entering', 'true');
  });

  it('virtualises long histories instead of rendering every message', () => {
    const state = withMessages(Array.from({ length: 600 }, (_, index) => message(`history-${index}`)));
    render(<MessageList bridge={createFakeBridge(state)} state={state} />);
    const timeline = screen.getByTestId('message-timeline');
    setScrollableGeometry(timeline, 0);

    fireEvent.scroll(timeline);

    const rendered = timeline.querySelectorAll('[data-testid^="message-history-"]').length;
    expect(rendered).toBeGreaterThan(0);
    expect(rendered).toBeLessThan(60);
    expect(screen.getByTestId('message-history-0')).toBeInTheDocument();
    expect(screen.queryByTestId('message-history-599')).toBeNull();
    expect(screen.queryByLabelText('正在加载消息')).toBeNull();
  });

  it('keeps a reader in history and reports how many received messages arrived', () => {
    const { rerender, bridge, timeline } = renderScrolledUp([receivedMessage('history-1')]);

    rerender(<MessageList bridge={bridge} state={withMessages([receivedMessage('history-1'), receivedMessage('new-1')])} />);

    expect(screen.getByRole('button', { name: '查看 1 条新消息' })).toBeInTheDocument();
    expect(timeline.scrollTop).toBe(0);
  });

  it('offers 回到底部 while scrolled up and jumps to the latest message with the new-message button', async () => {
    const { rerender, bridge, timeline } = renderScrolledUp([receivedMessage('history-1')]);
    expect(screen.getByRole('button', { name: '回到底部' })).toBeInTheDocument();

    rerender(<MessageList bridge={bridge} state={withMessages([receivedMessage('history-1'), receivedMessage('new-1')])} />);
    fireEvent.click(screen.getByRole('button', { name: '查看 1 条新消息' }));

    expect(timeline.scrollTop).toBe(1000);
    await vi.waitFor(() => expect(screen.queryByRole('button', { name: /新消息|回到底部/ })).toBeNull());
  });

  it('follows the latest message for the sender even after they reviewed history', () => {
    const { rerender, bridge, timeline } = renderScrolledUp([receivedMessage('history-1')]);

    rerender(<MessageList bridge={bridge} state={withMessages([receivedMessage('history-1'), message('self-1')])} />);

    expect(timeline.scrollTop).toBe(1000);
  });

  it('marks the first message that arrived while scrolled up with a 以下为新消息 divider', () => {
    const { rerender, bridge } = renderScrolledUp([receivedMessage('history-1')]);

    rerender(<MessageList bridge={bridge} state={withMessages([receivedMessage('history-1'), receivedMessage('new-1'), receivedMessage('new-2')])} />);

    const dividers = screen.getAllByRole('separator', { name: '以下为新消息' });
    expect(dividers).toHaveLength(1);
    expect(dividers[0].nextElementSibling).toBe(screen.getByTestId('message-new-1'));
  });

  it('shows no 以下为新消息 divider while following the latest message', () => {
    const first = withMessages([receivedMessage('history-1')]);
    const bridge = createFakeBridge(first);
    const { rerender } = render(<MessageList bridge={bridge} state={first} />);

    rerender(<MessageList bridge={bridge} state={withMessages([receivedMessage('history-1'), receivedMessage('new-1')])} />);

    expect(screen.queryByRole('separator', { name: '以下为新消息' })).not.toBeInTheDocument();
  });

  it('draws the divider above the unread messages a conversation was opened with', () => {
    const state = withMessages([receivedMessage('a'), receivedMessage('b'), receivedMessage('c')]);
    render(<MessageList bridge={createFakeBridge(state)} state={state} entryUnread={2} />);

    expect(screen.getByRole('separator', { name: '以下为新消息' }).nextElementSibling).toBe(screen.getByTestId('message-b'));
  });

  it('groups messages from the same author within five minutes and puts the tail on the last one', () => {
    const state = withMessages([
      receivedMessage('history-1', '10:00'),
      receivedMessage('history-2', '10:04'),
      receivedMessage('history-3', '10:12')
    ]);
    render(<MessageList bridge={createFakeBridge(state)} state={state} />);

    expect(screen.getByTestId('message-history-1')).not.toHaveClass('message--grouped');
    expect(screen.getByTestId('message-history-1')).not.toHaveClass('message--tail');
    expect(screen.getByTestId('message-history-2')).toHaveClass('message--grouped');
    expect(screen.getByTestId('message-history-2')).toHaveClass('message--tail');
    expect(screen.getByTestId('message-history-3')).not.toHaveClass('message--grouped');
  });

  it('never renders an empty bubble', () => {
    const state = withMessages([receivedMessage('ok'), { ...receivedMessage('blank'), content: '   ' }]);
    render(<MessageList bridge={createFakeBridge(state)} state={state} />);

    expect(screen.getByTestId('message-ok')).toBeInTheDocument();
    expect(screen.queryByTestId('message-blank')).toBeNull();
  });

  it('shows the sticky date pill above the history', () => {
    render(<MessageList bridge={createFakeBridge(baseState)} state={withMessages([message('history-1')])} />);

    expect(screen.getByTestId('date-separator')).toHaveTextContent('今天');
  });

  it('shows skeleton rows while connecting and an empty state for a quiet conversation', () => {
    const { rerender } = render(<MessageList bridge={createFakeBridge(baseState)} state={{ ...baseState, connection: { phase: 'reconnecting', statusText: '重连中', retryable: true } }} />);
    expect(screen.getByLabelText('正在加载消息')).toBeInTheDocument();

    rerender(<MessageList bridge={createFakeBridge(baseState)} state={baseState} />);
    expect(screen.getByText('这里还很安静')).toBeInTheDocument();
  });

  it('jumps to the quoted message and highlights it for a second', async () => {
    vi.useFakeTimers();
    try {
      const state = withMessages([receivedMessage('original'), { ...message('reply'), content: '> Bob: original\nthanks' }]);
      render(<MessageList bridge={createFakeBridge(state)} state={state} />);

      fireEvent.click(screen.getByTestId('message-quote-reply'));
      const bubble = screen.getByTestId('message-original').querySelector('.message-bubble')!;
      expect(bubble.className).toContain('ring-2');

      await act(async () => { vi.advanceTimersByTime(1100); });
      expect(screen.getByTestId('message-original').querySelector('.message-bubble')!.className).not.toContain('ring-2');
    } finally {
      vi.useRealTimers();
    }
  });
});
