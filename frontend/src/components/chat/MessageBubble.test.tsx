import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { createFakeBridge } from '../../bridge/chatBridge';
import type { BridgeState, MessageItem as MessageItemData } from '../../bridge/types';
import { MessageBubble, parseQuote } from './MessageBubble';

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
const peerMessage: MessageItemData = { ...message, messageId: 'm-2', displayName: 'Bob', userCode: 'B002', selfMessage: false };
const quotedMessage: MessageItemData = { ...message, messageId: 'm-3', content: '> Bob: hello\nI am replying here' };
const systemMessage: MessageItemData = {
  ...message, messageId: 'm-system', displayName: '', userCode: '', time: '10:02',
  content: 'Alice#A001 joined the chat', selfMessage: false, systemMessage: true
};

async function chooseMore(id: string, item: string) {
  fireEvent.keyDown(within(screen.getByLabelText(`actions-${id}`)).getByRole('button', { name: '更多操作' }), { key: 'Enter' });
  fireEvent.click(await screen.findByRole('menuitem', { name: item }));
}

describe('MessageBubble', () => {
  afterEach(cleanup);

  it('places the hover actions beside the bubble and keeps copy available', () => {
    const onCopy = vi.fn();
    render(<MessageBubble message={message} bridge={createFakeBridge(state)} onCopy={onCopy} />);

    const bubble = screen.getByTestId('message-content-m-1').closest('.message-bubble')!;
    const actions = screen.getByLabelText('actions-m-1');
    expect(bubble).not.toContainElement(actions);
    expect(bubble.nextElementSibling).toBe(actions);

    fireEvent.click(within(actions).getByRole('button', { name: '复制' }));
    expect(onCopy).toHaveBeenCalledWith(message);
  });

  it('forwards the complete message to the reply handler', () => {
    const onQuote = vi.fn();
    render(<MessageBubble message={message} bridge={createFakeBridge(state)} onQuote={onQuote} />);

    fireEvent.click(screen.getByRole('button', { name: '回复' }));
    expect(onQuote).toHaveBeenCalledWith(message);
  });

  it('copies the complete message text through the bridge command', () => {
    const bridge = createFakeBridge(state);
    render(<MessageBubble message={message} bridge={bridge} />);

    fireEvent.click(screen.getByRole('button', { name: '复制' }));

    expect(bridge.commands).toContainEqual(expect.objectContaining({ type: 'message.copy', payload: { text: 'hello' } }));
  });

  it('offers the same actions from the right-click menu', async () => {
    const onQuote = vi.fn();
    render(<MessageBubble message={peerMessage} bridge={createFakeBridge(state)} onQuote={onQuote} />);

    fireEvent.contextMenu(screen.getByTestId('message-content-m-2').closest('.message-bubble')!);
    fireEvent.click(await screen.findByRole('menuitem', { name: '回复' }));

    expect(onQuote).toHaveBeenCalledWith(peerMessage);
  });

  it('renders a quoted message on its own line above the reply and jumps to the original', () => {
    const onJumpToQuote = vi.fn();
    render(<MessageBubble message={quotedMessage} bridge={createFakeBridge(state)} onJumpToQuote={onJumpToQuote} />);

    const bubble = screen.getByTestId('message-m-3').querySelector('.message-bubble')!;
    const quote = screen.getByTestId('message-quote-m-3');
    const body = screen.getByTestId('message-content-m-3');

    expect(quote).toHaveTextContent('> Bob: hello');
    expect(body).toHaveTextContent('I am replying here');
    expect(quote.nextElementSibling).toBe(body);
    expect(bubble.firstElementChild).toBe(quote);
    fireEvent.click(quote);
    expect(onJumpToQuote).toHaveBeenCalledWith('Bob', 'hello');
  });

  it('parses only a leading quote line followed by a reply', () => {
    expect(parseQuote('> Bob: hi\nok')).toEqual({ quote: { raw: '> Bob: hi', author: 'Bob', text: 'hi' }, body: 'ok' });
    expect(parseQuote('> just a line')).toEqual({ quote: null, body: '> just a line' });
  });

  it('renders system notifications as centered text without a message bubble', () => {
    render(<MessageBubble message={systemMessage} bridge={createFakeBridge(state)} />);

    const item = screen.getByTestId('message-m-system');
    expect(item).toHaveClass('message--system');
    expect(item.querySelector('.message-bubble')).not.toBeInTheDocument();
    expect(item.querySelector('.message-meta')).not.toBeInTheDocument();
    expect(screen.getByTestId('message-content-m-system')).toHaveClass('message-system-text');
    expect(screen.getByTestId('message-content-m-system')).toHaveTextContent('Alice#A001 joined the chat');
  });

  it('offers 撤回 only when the workspace grants recall permission', async () => {
    const bridge = createFakeBridge(state);
    const { rerender } = render(<MessageBubble message={peerMessage} bridge={bridge} canRecall={false} />);

    // A peer message without recall rights has nothing beyond reply and copy.
    expect(within(screen.getByLabelText('actions-m-2')).queryByRole('button', { name: '更多操作' })).not.toBeInTheDocument();

    rerender(<MessageBubble message={peerMessage} bridge={bridge} canRecall />);
    await chooseMore('m-2', '撤回');

    expect(screen.queryByRole('menuitem', { name: '删除' })).not.toBeInTheDocument();
    expect(bridge.commands).toContainEqual(expect.objectContaining({ type: 'message.recall', payload: { messageId: 'm-2' } }));
  });

  it('keeps 删除 local to the author and shows result feedback for its own command', async () => {
    const bridge = createFakeBridge(state);
    render(<MessageBubble message={message} bridge={bridge} />);

    await chooseMore('m-1', '删除');

    expect(bridge.commands).toContainEqual(expect.objectContaining({ type: 'message.removeLocal', payload: { messageId: 'm-1' } }));
    await waitFor(() => expect(screen.getByRole('status')).toHaveTextContent('已在本地删除完成。'));
  });

  it('shows delivery ticks for each delivery state', () => {
    const bridge = createFakeBridge(state);
    const { rerender } = render(<MessageBubble message={{ ...message, deliveryState: 'queued' }} bridge={bridge} />);

    let delivery = screen.getByLabelText('投递状态：queued');
    expect(delivery).toHaveClass('message-delivery--queued');
    expect(delivery).toHaveTextContent('发送中');

    rerender(<MessageBubble message={{ ...message, deliveryState: 'sent' }} bridge={bridge} />);
    delivery = screen.getByLabelText('投递状态：sent');
    expect(delivery).toHaveAttribute('title', '已发送');
    expect(delivery.querySelectorAll('svg')).toHaveLength(1);

    rerender(<MessageBubble message={{ ...message, deliveryState: 'delivered' }} bridge={bridge} />);
    delivery = screen.getByLabelText('投递状态：delivered');
    expect(delivery).toHaveAttribute('title', '已送达');
    expect(delivery.querySelectorAll('svg')).toHaveLength(2);
    expect(delivery).toHaveClass('message-delivery--delivered');

    rerender(<MessageBubble message={{ ...message, deliveryState: 'failed' }} bridge={bridge} />);
    expect(screen.getByLabelText('投递状态：failed')).toHaveTextContent('发送失败');
  });

  it('retries a failed message from its status mark', () => {
    const bridge = createFakeBridge(state);
    render(<MessageBubble message={{ ...message, deliveryState: 'failed' }} bridge={bridge} />);

    fireEvent.click(screen.getByRole('button', { name: '重试发送' }));
    expect(bridge.commands).toContainEqual(expect.objectContaining({ type: 'message.retry', payload: { messageId: 'm-1' } }));
  });

  it('marks consecutive grouped messages, hides the author line and puts the tail on the last one', () => {
    const bridge = createFakeBridge(state);
    const { rerender } = render(<MessageBubble message={peerMessage} bridge={bridge} />);
    expect(screen.getByText('Bob')).toBeInTheDocument();
    expect(screen.getByTestId('message-m-2')).toHaveClass('message--tail');

    rerender(<MessageBubble message={peerMessage} bridge={bridge} grouped tail={false} />);

    expect(screen.getByTestId('message-m-2')).toHaveClass('message--grouped');
    expect(screen.getByTestId('message-m-2')).not.toHaveClass('message--tail');
    expect(screen.queryByText('Bob')).not.toBeInTheDocument();
  });

  it('omits sender names in direct conversations', () => {
    render(<MessageBubble message={peerMessage} bridge={createFakeBridge(state)} isGroupChat={false} />);
    expect(screen.queryByText('Bob')).not.toBeInTheDocument();
  });

  it('dispatches a download command for an available attachment', () => {
    const bridge = createFakeBridge(state);
    const attachmentMessage: MessageItemData = {
      ...peerMessage,
      messageId: 'm-file',
      attachment: { attachmentId: 'att-1', fileName: 'design.pdf', logicalSize: 3 * 1024 * 1024, status: 'available', receivedChunks: 3, totalChunks: 3 }
    };
    render(<MessageBubble message={attachmentMessage} bridge={bridge} />);

    const card = screen.getByTestId('message-attachment-att-1');
    expect(card).toHaveTextContent('design.pdf');
    expect(card).toHaveTextContent('3.0 MiB');
    expect(card).toHaveTextContent('端到端加密');
    const download = screen.getByRole('button', { name: '下载 design.pdf' });
    expect(download).toBeEnabled();
    fireEvent.click(download);
    expect(bridge.commands[0]).toMatchObject({ type: 'attachment.download', payload: { attachmentId: 'att-1', chunkIndex: 0 } });
  });

  it('marks expired attachments and explains the expiry', () => {
    const expiredMessage: MessageItemData = {
      ...peerMessage, messageId: 'm-expired',
      attachment: { attachmentId: 'att-2', fileName: 'notes.txt', logicalSize: 1024, status: 'expired' }
    };
    render(<MessageBubble message={expiredMessage} bridge={createFakeBridge(state)} />);

    expect(screen.getByTestId('message-attachment-att-2')).toHaveClass('message-attachment--expired');
    expect(screen.getByText('文件已过期（超过 24 小时），请让发送者重新发送')).toBeInTheDocument();
  });

  it('turns links into external anchors and code into code spans', () => {
    render(<MessageBubble message={{ ...peerMessage, content: 'see https://example.com/docs and `npm test`' }} bridge={createFakeBridge(state)} />);

    const link = screen.getByRole('link', { name: 'https://example.com/docs' });
    expect(link).toHaveAttribute('href', 'https://example.com/docs');
    expect(link).toHaveAttribute('rel', expect.stringContaining('noopener'));
    expect(screen.getByText('npm test').tagName).toBe('CODE');
  });

  it('never leaves a message invisible when its entrance animation is cancelled mid-flight', async () => {
    const bridge = createFakeBridge(state);
    const { rerender } = render(<MessageBubble message={peerMessage} bridge={bridge} animateEntry />);
    expect(screen.getByTestId('message-m-2')).toHaveAttribute('data-entering', 'true');
    // The list flips animateEntry off on the very next render, before the animation can finish.
    rerender(<MessageBubble message={peerMessage} bridge={bridge} animateEntry={false} />);

    const article = screen.getByTestId('message-m-2');
    expect(article).not.toHaveAttribute('data-entering');
    await waitFor(() => expect(article.style.opacity).toBe('1'));
  });
});
