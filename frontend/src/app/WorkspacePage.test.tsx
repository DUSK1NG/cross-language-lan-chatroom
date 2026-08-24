import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

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
    fireEvent.click(screen.getByRole('button', { name: '私' }));
    fireEvent.click(screen.getByRole('button', { name: 'direct-bob' }));

    expect(bridge.commands.map(({ type, payload }) => ({ type, payload }))).toEqual([
      { type: 'conversation.selectRoom', payload: { room: 'study' } },
      { type: 'conversation.selectDirect', payload: { userCode: 'B002' } }
    ]);
  });

  it('separates 群 and 私 navigation lists', () => {
    const bridge = createFakeBridge(workspaceState);

    render(<WorkspacePage bridge={bridge} state={workspaceState} />);

    expect(screen.getByRole('button', { name: 'room-study' })).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'direct-bob' })).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: '私' }));

    expect(screen.getByRole('button', { name: 'direct-bob' })).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'room-study' })).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: '群' }));
    expect(screen.getByRole('button', { name: 'room-study' })).toBeInTheDocument();
  });

  it('opens settings from the Chinese workspace navigation', () => {
    const bridge = createFakeBridge(workspaceState);
    const onSettings = vi.fn();

    render(<WorkspacePage bridge={bridge} state={workspaceState} onSettings={onSettings} />);
    fireEvent.click(screen.getByRole('button', { name: '设置' }));

    expect(onSettings).toHaveBeenCalledOnce();
  });

  it('opens the member drawer from the member action', () => {
    const bridge = createFakeBridge(workspaceState);

    render(<WorkspacePage bridge={bridge} state={workspaceState} />);
    fireEvent.click(screen.getByRole('button', { name: 'members-toggle' }));

    const dialog = screen.getByRole('dialog', { name: '成员' });
    expect(dialog).toBeInTheDocument();
    expect(within(dialog).getByText('Bob')).toBeInTheDocument();
  });

  it('opens a private conversation for another member but not the local member', () => {
    const bridge = createFakeBridge(workspaceState);

    render(<WorkspacePage bridge={bridge} state={workspaceState} />);
    fireEvent.click(screen.getByRole('button', { name: 'members-toggle' }));

    const dialog = screen.getByRole('dialog', { name: '成员' });
    expect(within(dialog).queryByRole('button', { name: 'direct-Alice' })).not.toBeInTheDocument();
    fireEvent.click(within(dialog).getByRole('button', { name: 'direct-Bob' }));
    expect(bridge.commands).toContainEqual(expect.objectContaining({
      type: 'conversation.openPrivate',
      payload: { displayName: 'Bob', userCode: 'B002' }
    }));
  });

  it('keeps one read-only quote while leaving the reply editable', () => {
    const bridge = createFakeBridge(workspaceState);

    render(<WorkspacePage bridge={bridge} state={workspaceState} />);
    const composer = screen.getByLabelText('消息输入框') as HTMLTextAreaElement;
    fireEvent.change(composer, { target: { value: 'Follow-up' } });
    fireEvent.click(screen.getAllByRole('button', { name: '引用' })[1]);

    expect(screen.getByTestId('composer-quote')).toHaveTextContent('> Bob: A very long message that must remain readable when the viewport is narrow.');
    expect(composer).toHaveValue('Follow-up');
    expect(composer).toHaveFocus();
    expect(composer.selectionStart).toBe(composer.value.length);
    expect(composer.selectionEnd).toBe(composer.value.length);

    fireEvent.click(screen.getAllByRole('button', { name: '引用' })[0]);
    expect(screen.getByTestId('composer-quote')).toHaveTextContent('> Alice: self message');
    expect(screen.getByTestId('composer-quote')).not.toHaveTextContent('Bob');
    expect(composer).toHaveValue('Follow-up');
  });

  it('quotes only the current reply when quoting an already quoted message', () => {
    const quotedWorkspaceState: BridgeState = {
      ...workspaceState,
      activeMessages: [...workspaceState.activeMessages, {
        messageId: 'm-3', displayName: 'Bob', userCode: 'B002', time: '10:03',
        content: '> Alice: HELLO\nHI', selfMessage: false, systemMessage: false
      }]
    };
    const bridge = createFakeBridge(quotedWorkspaceState);

    render(<WorkspacePage bridge={bridge} state={quotedWorkspaceState} />);
    const composer = screen.getByLabelText('消息输入框');
    fireEvent.change(composer, { target: { value: 'Follow-up' } });
    fireEvent.click(within(screen.getByTestId('message-m-3')).getByRole('button', { name: '引用' }));

    expect(composer).toHaveValue('Follow-up');
    expect(screen.getByTestId('composer-quote')).toHaveTextContent('> Bob: HI');
  });

  it('replaces the current quote instead of appending another quote', () => {
    const bridge = createFakeBridge(workspaceState);

    render(<WorkspacePage bridge={bridge} state={workspaceState} />);
    const composer = screen.getByLabelText('消息输入框') as HTMLTextAreaElement;
    fireEvent.click(within(screen.getByTestId('message-m-1')).getByRole('button', { name: '引用' }));
    fireEvent.click(within(screen.getByTestId('message-m-2')).getByRole('button', { name: '引用' }));

    expect(screen.getByTestId('composer-quote')).toHaveTextContent('> Bob: A very long message that must remain readable when the viewport is narrow.');
    expect(screen.getByTestId('composer-quote')).not.toHaveTextContent('Alice');
    expect(composer).toHaveValue('');
  });

  it('closes the member drawer with Escape', () => {
    const bridge = createFakeBridge(workspaceState);

    render(<WorkspacePage bridge={bridge} state={workspaceState} />);
    fireEvent.click(screen.getByRole('button', { name: 'members-toggle' }));
    fireEvent.keyDown(window, { key: 'Escape' });

    expect(screen.queryByRole('dialog', { name: '成员' })).not.toBeInTheDocument();
  });

  it('creates a private room then refreshes rooms on success', async () => {
    const bridge = createFakeBridge(workspaceState);

    render(<WorkspacePage bridge={bridge} state={workspaceState} />);
    fireEvent.click(screen.getByRole('button', { name: '新建频道' }));
    fireEvent.change(screen.getByLabelText('频道名称'), { target: { value: 'study_group' } });
    fireEvent.click(screen.getByLabelText('私有频道'));
    fireEvent.click(screen.getByRole('button', { name: '确认新建频道' }));

    expect(bridge.commands[0]).toMatchObject({
      type: 'room.create',
      payload: { room: 'study_group', isPrivate: true }
    });
    await waitFor(() => expect(bridge.commands[1]).toMatchObject({ type: 'directory.refreshRooms', payload: {} }));
    expect(screen.queryByRole('dialog', { name: '新建频道' })).not.toBeInTheDocument();
  });

  it('keeps room details and prevents duplicate submissions until the command resolves', async () => {
    const bridge = createFakeBridge(workspaceState);

    render(<WorkspacePage bridge={bridge} state={workspaceState} />);
    fireEvent.click(screen.getByRole('button', { name: '新建频道' }));
    fireEvent.change(screen.getByLabelText('频道名称'), { target: { value: 'project' } });
    const submit = screen.getByRole('button', { name: '确认新建频道' });
    fireEvent.click(submit);
    fireEvent.click(submit);

    expect(bridge.commands.filter(({ type }) => type === 'room.create')).toHaveLength(1);
    await waitFor(() => expect(screen.queryByRole('dialog', { name: '新建频道' })).not.toBeInTheDocument());
  });

  it('filters the active 群 or 私 list with the search box', () => {
    const bridge = createFakeBridge(workspaceState);
    render(<WorkspacePage bridge={bridge} state={workspaceState} />);

    fireEvent.change(screen.getByRole('searchbox', { name: '搜索群聊' }), { target: { value: 'study' } });
    expect(screen.getByRole('button', { name: 'room-study' })).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'room-lobby' })).not.toBeInTheDocument();
  });

  it('opens member administration actions for users when permitted', () => {
    const state = {
      ...workspaceState,
      members: workspaceState.members.map((member) => member.userCode === 'B002' ? { ...member, admin: false } : member),
      permissions: { activeRoomCanManage: true }
    };
    const bridge = createFakeBridge(state);
    render(<WorkspacePage bridge={bridge} state={state} />);

    fireEvent.click(screen.getByRole('button', { name: '管理 Bob' }));
    fireEvent.click(screen.getByRole('button', { name: '禁言/解禁' }));

    expect(screen.getByRole('dialog', { name: '确认切换禁言' })).toBeInTheDocument();
    expect(bridge.commands).toHaveLength(0);
    fireEvent.click(screen.getByRole('button', { name: '确认' }));

    expect(bridge.commands).toContainEqual(expect.objectContaining({
      type: 'admin.action', payload: { action: 'mute', targetUserCode: 'B002' }
    }));
  });

  it('lets an administrator resolve a pending member connection', () => {
    const state: BridgeState = {
      ...workspaceState,
      identity: { ...workspaceState.identity, admin: true },
      connectionApprovals: [{ id: '42', displayName: 'Cara', userCode: 'C003', requestedAt: '2026-08-24T10:00:00Z' }]
    };
    const bridge = createFakeBridge(state);
    render(<WorkspacePage bridge={bridge} state={state} />);

    fireEvent.click(screen.getByRole('button', { name: '连接审批' }));
    const dialog = screen.getByRole('dialog', { name: '连接审批' });
    expect(within(dialog).getByText('Cara#C003')).toBeInTheDocument();
    fireEvent.click(within(dialog).getByRole('button', { name: '允许连接' }));

    expect(bridge.commands).toContainEqual(expect.objectContaining({
      type: 'admin.action',
      payload: { action: 'approve_connection', targetUserCode: 'C003', messageId: '42' }
    }));
  });

  it('opens connection approval immediately when an administrator receives a member request', () => {
    const state: BridgeState = {
      ...workspaceState,
      identity: { ...workspaceState.identity, admin: true },
      connectionApprovals: [{ id: '42', displayName: 'Cara', userCode: 'C003', requestedAt: '2026-08-24T10:00:00Z' }]
    };
    const bridge = createFakeBridge(state);
    render(<WorkspacePage bridge={bridge} state={state} />);

    expect(screen.getByRole('dialog', { name: '连接审批' })).toBeInTheDocument();
    expect(screen.getByText('Cara#C003')).toBeInTheDocument();
  });

  it('requires an owner decision before an active connection approval can close', () => {
    const state: BridgeState = {
      ...workspaceState,
      identity: { ...workspaceState.identity, admin: true },
      connectionApprovals: [{ id: '42', displayName: 'Cara', userCode: 'C003', requestedAt: '2026-08-24T10:00:00Z' }]
    };
    const bridge = createFakeBridge(state);
    render(<WorkspacePage bridge={bridge} state={state} />);

    const dialog = screen.getByRole('dialog', { name: '连接审批' });
    fireEvent.click(dialog.parentElement!);
    fireEvent.keyDown(window, { key: 'Escape' });

    expect(screen.getByRole('dialog', { name: '连接审批' })).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'close' })).not.toBeInTheDocument();
  });

  it('keeps the administrator connection approval inbox available when it is empty', () => {
    const state: BridgeState = {
      ...workspaceState,
      identity: { ...workspaceState.identity, admin: true },
      connectionApprovals: []
    };
    const bridge = createFakeBridge(state);
    render(<WorkspacePage bridge={bridge} state={state} />);

    expect(screen.getByRole('button', { name: '连接审批' })).toBeInTheDocument();
  });

  it('opens a member profile with online state and admin badge', () => {
    const bridge = createFakeBridge(workspaceState);
    render(<WorkspacePage bridge={bridge} state={workspaceState} />);

    fireEvent.click(screen.getByRole('button', { name: '查看 Bob' }));

    const dialog = screen.getByRole('dialog', { name: '成员资料' });
    expect(within(dialog).getByText('Bob#B002')).toBeInTheDocument();
    expect(within(dialog).getByText(/管理员/)).toBeInTheDocument();
    expect(within(dialog).getByText(/在线/)).toBeInTheDocument();
  });

  it('rejects invalid channel names before dispatching creation', () => {
    const bridge = createFakeBridge(workspaceState);
    render(<WorkspacePage bridge={bridge} state={workspaceState} />);

    fireEvent.click(screen.getByRole('button', { name: '新建频道' }));
    fireEvent.change(screen.getByLabelText('频道名称'), { target: { value: 'bad name' } });

    expect(screen.getByText('只能使用字母、数字和下划线，长度 1-32 位')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: '确认新建频道' })).toBeDisabled();
    expect(bridge.commands).toHaveLength(0);
  });

  it('shows completion feedback for channel actions', async () => {
    const bridge = createFakeBridge({ ...workspaceState, permissions: { activeRoomCanManage: true } });
    render(<WorkspacePage bridge={bridge} state={{ ...workspaceState, permissions: { activeRoomCanManage: true } }} />);

    fireEvent.click(screen.getByRole('button', { name: '频道管理' }));
    fireEvent.change(screen.getByRole('textbox', { name: '成员代码' }), { target: { value: 'B003' } });
    fireEvent.click(screen.getByRole('button', { name: '邀请成员' }));

    expect(await screen.findByRole('status')).toHaveTextContent('操作成功');
  });

  it('reports online members rather than total member records', () => {
    const state = {
      ...workspaceState,
      members: [...workspaceState.members, { displayName: 'Cara', userCode: 'C003', online: false, admin: false }]
    };
    render(<WorkspacePage bridge={createFakeBridge(state)} state={state} />);

    expect(screen.getByLabelText('在线成员数量')).toHaveTextContent('2');
    expect(screen.getByText('2 名在线成员')).toBeInTheDocument();
  });

  it('opens navigation from the compact rail entry', () => {
    const bridge = createFakeBridge(workspaceState);
    render(<WorkspacePage bridge={bridge} state={workspaceState} />);

    fireEvent.click(screen.getByRole('button', { name: '打开导航' }));

    const dialog = screen.getByRole('dialog', { name: '群聊导航' });
    expect(within(dialog).getByRole('button', { name: 'room-study' })).toBeInTheDocument();
  });

  it('dispatches channel invite and removal actions from channel management', async () => {
    const bridge = createFakeBridge({ ...workspaceState, permissions: { activeRoomCanManage: true } });
    render(<WorkspacePage bridge={bridge} state={{ ...workspaceState, permissions: { activeRoomCanManage: true } }} />);

    fireEvent.click(screen.getByRole('button', { name: '频道管理' }));
    fireEvent.change(screen.getByRole('textbox', { name: '成员代码' }), { target: { value: 'B002' } });
    fireEvent.click(screen.getByRole('button', { name: '邀请成员' }));
    await waitFor(() => expect(screen.getByRole('status')).toHaveTextContent('操作成功'));
    fireEvent.change(screen.getByRole('textbox', { name: '成员代码' }), { target: { value: 'B003' } });
    fireEvent.click(screen.getByRole('button', { name: '移除成员' }));

    expect(bridge.commands).toEqual(expect.arrayContaining([
      expect.objectContaining({ type: 'room.action', payload: { action: 'invite', room: 'lobby', targetUserCode: 'B002' } }),
      expect.objectContaining({ type: 'room.action', payload: { action: 'remove_member', room: 'lobby', targetUserCode: 'B003' } })
    ]));
  });
});
