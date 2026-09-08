import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { createFakeBridge } from '../bridge/chatBridge';
import type { BridgeState, MessageItem as MessageItemData } from '../bridge/types';
import { MessageItem } from './MessageItem';

const state: BridgeState = {
  schemaVersion: 1,
  connection: { phase: 'connected', statusText: 'Connected', retryable: false },
  identity: { displayName: 'Alice', userCode: 'A001', admin: false },
  navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
  rooms: [], directMessages: [], activeMessages: [], members: [],
  permissions: { activeRoomCanManage: false },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' }
};

const message: MessageItemData = {
  messageId: 'm-1', displayName: 'Alice', userCode: 'A001', time: '10:01',
  content: 'hello', selfMessage: true, systemMessage: false
};

const peerMessage: MessageItemData = {
  ...message,
  messageId: 'm-2',
  displayName: 'Bob',
  userCode: 'B002',
  selfMessage: false
};

const quotedMessage: MessageItemData = {
  ...message,
  messageId: 'm-3',
  content: '> Bob: hello\nI am replying here',
};

const systemMessage: MessageItemData = {
  ...message,
  messageId: 'm-system',
  displayName: '',
  userCode: '',
  time: '10:02',
  content: 'Alice#A001 joined the chat',
  selfMessage: false,
  systemMessage: true
};

describe('MessageItem', () => {
  afterEach(cleanup);

  it('places actions below the message bubble and keeps copy available', () => {
    const bridge = createFakeBridge(state);
    const onCopy = vi.fn();

    render(<MessageItem message={message} bridge={bridge} onCopy={onCopy} />);

    const bubble = screen.getByTestId('message-content-m-1').closest('.message-bubble')!;
    const actions = screen.getByLabelText('actions-m-1');
    expect(bubble).not.toContainElement(actions);
    expect(bubble.nextElementSibling).toBe(actions);

    fireEvent.click(screen.getByRole('button', { name: '复制' }));
    expect(onCopy).toHaveBeenCalledWith(message);
  });

  it('forwards the complete message to the quote handler', () => {
    const bridge = createFakeBridge(state);
    const onQuote = vi.fn();

    render(<MessageItem message={message} bridge={bridge} onQuote={onQuote} />);

    fireEvent.click(screen.getByRole('button', { name: '引用' }));
    expect(onQuote).toHaveBeenCalledWith(message);
  });

  it('copies the complete message text through the bridge command', () => {
    const bridge = createFakeBridge(state);

    render(<MessageItem message={message} bridge={bridge} />);

    fireEvent.click(screen.getByRole('button', { name: '复制' }));

    expect(bridge.commands).toContainEqual(expect.objectContaining({
      type: 'message.copy',
      payload: { text: 'hello' }
    }));
  });

  it('renders a quoted message on its own line above the reply', () => {
    const bridge = createFakeBridge(state);

    render(<MessageItem message={quotedMessage} bridge={bridge} />);

    const bubble = screen.getByTestId('message-m-3').querySelector('.message-bubble')!;
    const quote = screen.getByTestId('message-quote-m-3');
    const body = screen.getByTestId('message-content-m-3');

    expect(quote).toHaveTextContent('> Bob: hello');
    expect(body).toHaveTextContent('I am replying here');
    expect(quote.nextElementSibling).toBe(body);
    expect(bubble.firstElementChild).toBe(quote);
  });

  it('renders system notifications as centered text without a message bubble', () => {
    const bridge = createFakeBridge(state);

    render(<MessageItem message={systemMessage} bridge={bridge} />);

    const item = screen.getByTestId('message-m-system');
    expect(item).toHaveClass('message--system');
    expect(item.querySelector('.message-bubble')).not.toBeInTheDocument();
    expect(item.querySelector('.message-meta')).not.toBeInTheDocument();
    expect(screen.getByTestId('message-content-m-system')).toHaveClass('message-system-text');
    expect(screen.getByTestId('message-content-m-system')).toHaveTextContent('Alice#A001 joined the chat');
  });

  it('shows Recall only when the workspace grants recall permission', () => {
    const bridge = createFakeBridge(state);
    const { rerender } = render(<MessageItem message={peerMessage} bridge={bridge} canRecall={false} />);

    expect(screen.queryByRole('button', { name: '撤回' })).not.toBeInTheDocument();
    expect(screen.queryByRole('button', { name: '删除' })).not.toBeInTheDocument();

    rerender(<MessageItem message={peerMessage} bridge={bridge} canRecall />);

    fireEvent.click(screen.getByRole('button', { name: '撤回' }));
    expect(bridge.commands).toContainEqual(expect.objectContaining({
      type: 'message.recall',
      payload: { messageId: 'm-2' }
    }));
  });

  it('keeps Remove local to the author and shows result feedback for its own command', async () => {
    const bridge = createFakeBridge(state);
    render(<MessageItem message={message} bridge={bridge} />);

    fireEvent.click(screen.getByRole('button', { name: '删除' }));

    expect(bridge.commands).toContainEqual(expect.objectContaining({
      type: 'message.removeLocal',
      payload: { messageId: 'm-1' }
    }));
    await waitFor(() => expect(screen.getByRole('status')).toHaveTextContent('已在本地删除完成。'));
  });

  it('shows delivery ticks for each delivery state', () => {
    const bridge = createFakeBridge(state);
    const { rerender } = render(<MessageItem message={{ ...message, deliveryState: 'queued' }} bridge={bridge} />);

    let delivery = screen.getByLabelText('投递状态：queued');
    expect(delivery).toHaveClass('message-delivery--queued');
    expect(delivery).toHaveTextContent('⏱ 发送中');

    rerender(<MessageItem message={{ ...message, deliveryState: 'delivered' }} bridge={bridge} />);
    delivery = screen.getByLabelText('投递状态：delivered');
    expect(delivery).toHaveTextContent('✓✓');
    expect(delivery).toHaveClass('message-delivery--delivered');

    rerender(<MessageItem message={{ ...message, deliveryState: 'failed' }} bridge={bridge} />);
    expect(screen.getByLabelText('投递状态：failed')).toHaveTextContent('⚠');
  });

  it('marks consecutive grouped messages and hides the author line', () => {
    const bridge = createFakeBridge(state);

    const { rerender } = render(<MessageItem message={peerMessage} bridge={bridge} />);
    expect(screen.getByText('Bob')).toBeInTheDocument();

    rerender(<MessageItem message={peerMessage} bridge={bridge} grouped />);

    expect(screen.getByTestId('message-m-2')).toHaveClass('message--grouped');
    expect(screen.queryByText('Bob')).not.toBeInTheDocument();
  });

  it('dispatches a download command for an available attachment', () => {
    const bridge = createFakeBridge(state);
    const attachmentMessage: MessageItemData = {
      ...peerMessage,
      messageId: 'm-file',
      attachment: {
        attachmentId: 'att-1', fileName: 'design.pdf', logicalSize: 3 * 1024 * 1024,
        status: 'available', receivedChunks: 3, totalChunks: 3
      }
    };

    render(<MessageItem message={attachmentMessage} bridge={bridge} />);

    const card = screen.getByTestId('message-attachment-att-1');
    expect(card).toHaveTextContent('design.pdf');
    expect(card).toHaveTextContent('3.0 MiB');
    expect(card).toHaveTextContent('🔒 端到端加密');
    const download = screen.getByRole('button', { name: '下载 design.pdf' });
    expect(download).toBeEnabled();
    fireEvent.click(download);
    expect(bridge.commands[0]).toMatchObject({
      type: 'attachment.download',
      payload: { attachmentId: 'att-1', chunkIndex: 0 }
    });
  });

  it('marks expired attachments and explains the expiry', () => {
    const bridge = createFakeBridge(state);
    const expiredMessage: MessageItemData = {
      ...peerMessage,
      messageId: 'm-expired',
      attachment: { attachmentId: 'att-2', fileName: 'notes.txt', logicalSize: 1024, status: 'expired' }
    };

    render(<MessageItem message={expiredMessage} bridge={bridge} />);

    expect(screen.getByTestId('message-attachment-att-2')).toHaveClass('message-attachment--expired');
    expect(screen.getByText('文件已过期（超过 24 小时），请让发送者重新发送')).toBeInTheDocument();
  });
});
