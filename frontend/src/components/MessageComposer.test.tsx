import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import type { AttachmentEvent, BridgeCommand, BridgeError, BridgeState, ChatBridgeClient, CommandResult } from '../bridge/types';
import { MessageComposer } from './MessageComposer';

const connectedRoomState: BridgeState = {
  schemaVersion: 1,
  connection: { phase: 'connected', statusText: 'Connected', retryable: false },
  identity: { displayName: 'Alice', userCode: 'A001', admin: false },
  navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
  rooms: [], directMessages: [], activeMessages: [], members: [],
  permissions: { activeRoomCanManage: false },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' }
};

const rejectedError: BridgeError = {
  code: 'rejected', message: 'rejected', retryable: false, source: 'server'
};

class ControllableBridge implements ChatBridgeClient {
  readonly commands: BridgeCommand[] = [];
  private readonly resultListeners = new Set<(result: CommandResult) => void>();
  private readonly attachmentListeners = new Set<(event: AttachmentEvent) => void>();

  currentStateJson() { return JSON.stringify(connectedRoomState); }
  dispatch(command: BridgeCommand) { this.commands.push(command); }
  subscribe() { return () => undefined; }
  subscribeCommandResult(listener: (result: CommandResult) => void) {
    this.resultListeners.add(listener);
    return () => this.resultListeners.delete(listener);
  }
  subscribeAttachmentEvents(listener: (event: AttachmentEvent) => void) {
    this.attachmentListeners.add(listener);
    return () => this.attachmentListeners.delete(listener);
  }
  subscribeBridgeError() { return () => undefined; }
  publishCommandResult(result: CommandResult) { this.resultListeners.forEach((listener) => listener(result)); }
  publishAttachmentEvent(event: AttachmentEvent) { this.attachmentListeners.forEach((listener) => listener(event)); }
}

describe('MessageComposer', () => {
  afterEach(cleanup);

  it('disables send until the draft contains non-whitespace text', () => {
    const bridge = new ControllableBridge();
    render(<MessageComposer state={connectedRoomState} bridge={bridge} draft="   " onDraftChange={vi.fn()} onCommandResult={vi.fn()} />);

    expect(screen.getByRole('button', { name: '发送消息' })).toBeDisabled();
    expect(bridge.commands).toHaveLength(0);
  });

  it('keeps a failed send draft and reports the error', () => {
    const bridge = new ControllableBridge();
    render(<MessageComposer state={connectedRoomState} bridge={bridge} draft="retry" onDraftChange={vi.fn()} onCommandResult={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '发送消息' }));
    act(() => bridge.publishCommandResult({ id: bridge.commands[0].id, ok: false, error: rejectedError }));

    expect(screen.getByLabelText('消息输入框')).toHaveValue('retry');
    expect(screen.getByRole('alert')).toHaveTextContent('rejected');
  });

  it('clears the controlled draft after a successful send', () => {
    const bridge = new ControllableBridge();
    const onDraftChange = vi.fn();
    render(<MessageComposer state={connectedRoomState} bridge={bridge} draft="sent" onDraftChange={onDraftChange} onCommandResult={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '发送消息' }));
    act(() => bridge.publishCommandResult({ id: bridge.commands[0].id, ok: true }));

    expect(onDraftChange).toHaveBeenCalledWith('');
  });

  it('preserves text entered while a send is pending', () => {
    const bridge = new ControllableBridge();
    const onDraftChange = vi.fn();
    const { rerender } = render(<MessageComposer state={connectedRoomState} bridge={bridge} draft="sent" onDraftChange={onDraftChange} onCommandResult={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '发送消息' }));
    const commandId = bridge.commands[0].id;
    rerender(<MessageComposer state={connectedRoomState} bridge={bridge} draft="new text" onDraftChange={onDraftChange} onCommandResult={vi.fn()} />);
    act(() => bridge.publishCommandResult({ id: commandId, ok: true }));

    expect(onDraftChange).not.toHaveBeenCalledWith('');
    expect(screen.getByLabelText('消息输入框')).toHaveValue('new text');
  });

  it('gates a second send while the first command is pending', () => {
    const bridge = new ControllableBridge();
    render(<MessageComposer state={connectedRoomState} bridge={bridge} draft="one message" onDraftChange={vi.fn()} onCommandResult={vi.fn()} />);

    const sendButton = screen.getByRole('button', { name: '发送消息' });
    fireEvent.click(sendButton);
    fireEvent.click(sendButton);

    expect(bridge.commands).toHaveLength(1);
    expect(sendButton).toBeDisabled();
  });

  it('submits on Enter and preserves a newline on Shift+Enter', () => {
    const bridge = new ControllableBridge();
    render(<MessageComposer state={connectedRoomState} bridge={bridge} draft="quoted" onDraftChange={vi.fn()} onCommandResult={vi.fn()} />);

    const composer = screen.getByLabelText('消息输入框');
    fireEvent.keyDown(composer, { key: 'Enter', shiftKey: true });
    expect(bridge.commands).toHaveLength(0);
    fireEvent.keyDown(composer, { key: 'Enter' });

    expect(bridge.commands).toHaveLength(1);
  });

  it('ignores a matching result after unmount', () => {
    const bridge = new ControllableBridge();
    const onDraftChange = vi.fn();
    const onCommandResult = vi.fn();
    const { unmount } = render(<MessageComposer state={connectedRoomState} bridge={bridge} draft="leave" onDraftChange={onDraftChange} onCommandResult={onCommandResult} />);

    fireEvent.click(screen.getByRole('button', { name: '发送消息' }));
    const commandId = bridge.commands[0].id;
    unmount();
    act(() => bridge.publishCommandResult({ id: commandId, ok: true }));

    expect(onCommandResult).not.toHaveBeenCalled();
    expect(onDraftChange).not.toHaveBeenCalled();
  });

  it('keeps the quote read-only and sends it together with the reply', () => {
    const bridge = new ControllableBridge();
    const onDraftChange = vi.fn();
    const { rerender } = render(
      <MessageComposer
        state={connectedRoomState}
        bridge={bridge}
        quote={{ displayName: 'Bob', content: 'hello' }}
        draft="My reply"
        onDraftChange={onDraftChange}
        onCommandResult={vi.fn()}
      />
    );

    expect(screen.getByTestId('composer-quote')).toHaveTextContent('> Bob: hello');
    expect(screen.getByLabelText('消息输入框')).toHaveValue('My reply');
    expect(screen.queryByRole('textbox', { name: 'quoted message' })).not.toBeInTheDocument();

    fireEvent.change(screen.getByLabelText('消息输入框'), { target: { value: 'Edited reply' } });
    rerender(
      <MessageComposer
        state={connectedRoomState}
        bridge={bridge}
        quote={{ displayName: 'Bob', content: 'hello' }}
        draft="Edited reply"
        onDraftChange={onDraftChange}
        onCommandResult={vi.fn()}
      />
    );
    fireEvent.click(screen.getByRole('button', { name: '发送消息' }));

    expect(bridge.commands[0]).toMatchObject({
      type: 'chat.sendRoom',
      payload: { room: 'lobby', content: '> Bob: hello\nEdited reply' }
    });
  });

  it('inserts an emoji at the current cursor position', () => {
    const bridge = new ControllableBridge();
    const onDraftChange = vi.fn();
    render(<MessageComposer state={connectedRoomState} bridge={bridge} draft="hi" onDraftChange={onDraftChange} onCommandResult={vi.fn()} />);

    const composer = screen.getByLabelText('消息输入框') as HTMLTextAreaElement;
    composer.focus();
    composer.setSelectionRange(1, 1);
    fireEvent.click(screen.getByRole('button', { name: '表情' }));
    fireEvent.click(screen.getByRole('button', { name: '😀' }));

    expect(onDraftChange).toHaveBeenLastCalledWith('h😀i');
  });
});
