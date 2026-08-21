import { cleanup, fireEvent, render, screen, within } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import { createFakeBridge } from '../bridge/chatBridge';
import type { BridgeState } from '../bridge/types';
import { WorkspacePage } from './WorkspacePage';

const workspaceState: BridgeState = {
  schemaVersion: 1,
  connection: { phase: 'connected', statusText: 'Connected', retryable: false },
  identity: { displayName: 'Alice', userCode: 'A001', admin: false },
  navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
  rooms: [
    { roomName: 'lobby', memberCount: 2, unreadCount: 3 },
    { roomName: 'study', memberCount: 4, unreadCount: 0 }
  ],
  directMessages: [{ displayName: 'Bob', userCode: 'B002', unreadCount: 1 }],
  activeMessages: [
    {
      messageId: 'm-1', displayName: 'Alice', userCode: 'A001', time: '10:01',
      content: 'self message', selfMessage: true, systemMessage: false
    },
    {
      messageId: 'm-2', displayName: 'Bob', userCode: 'B002', time: '10:02',
      content: 'A very long message that must remain readable when the viewport is narrow.',
      selfMessage: false, systemMessage: false
    }
  ],
  members: [
    { displayName: 'Alice', userCode: 'A001', online: true, admin: false },
    { displayName: 'Bob', userCode: 'B002', online: true, admin: true }
  ],
  permissions: { activeRoomCanManage: false },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' }
};

describe('WorkspacePage', () => {
  afterEach(cleanup);

  it('renders four regions and keeps the identity card outside the message scroller', () => {
    const bridge = createFakeBridge(workspaceState);

    render(<WorkspacePage bridge={bridge} state={workspaceState} />);

    expect(screen.getByTestId('workspace-rail')).toBeInTheDocument();
    expect(screen.getByTestId('conversation-sidebar')).toBeInTheDocument();
    expect(screen.getByTestId('message-timeline')).toBeInTheDocument();
    expect(screen.getByTestId('member-panel')).toBeInTheDocument();
    expect(screen.getByTestId('identity-card')).toHaveClass('identity-card--fixed');
  });

  it('aligns self messages as a cluster and wraps long content', () => {
    const bridge = createFakeBridge(workspaceState);

    render(<WorkspacePage bridge={bridge} state={workspaceState} />);

    expect(screen.getByTestId('message-m-1')).toHaveClass('message--self');
    expect(screen.getByTestId('message-m-2')).toHaveClass('message--peer');
    expect(screen.getByTestId('message-content-m-2')).toHaveClass('message-content--wrap');
  });

  it('isolates room and direct-message actions through typed bridge commands', () => {
    const bridge = createFakeBridge(workspaceState);

    render(<WorkspacePage bridge={bridge} state={workspaceState} />);
    fireEvent.click(screen.getByRole('button', { name: 'room-study' }));
    fireEvent.click(screen.getByRole('button', { name: 'direct-bob' }));

    expect(bridge.commands.map(({ type, payload }) => ({ type, payload }))).toEqual([
      { type: 'conversation.selectRoom', payload: { room: 'study' } },
      { type: 'conversation.selectDirect', payload: { userCode: 'B002' } }
    ]);
  });

  it('opens the member drawer from the member action', () => {
    const bridge = createFakeBridge(workspaceState);

    render(<WorkspacePage bridge={bridge} state={workspaceState} />);
    fireEvent.click(screen.getByRole('button', { name: 'members-toggle' }));

    const dialog = screen.getByRole('dialog', { name: 'Members' });
    expect(dialog).toBeInTheDocument();
    expect(within(dialog).getByText('Bob')).toBeInTheDocument();
  });

  it('closes the member drawer with Escape', () => {
    const bridge = createFakeBridge(workspaceState);

    render(<WorkspacePage bridge={bridge} state={workspaceState} />);
    fireEvent.click(screen.getByRole('button', { name: 'members-toggle' }));
    fireEvent.keyDown(window, { key: 'Escape' });

    expect(screen.queryByRole('dialog', { name: 'Members' })).not.toBeInTheDocument();
  });
});
