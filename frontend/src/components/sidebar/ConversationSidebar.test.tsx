import { cleanup, fireEvent, render, screen, within } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { createFakeBridge } from '../../bridge/chatBridge';
import type { BridgeState } from '../../bridge/types';
import { TooltipProvider } from '../ui/tooltip';
import { ConversationSidebar } from './ConversationSidebar';
import { buildEntries, formatUnread } from './conversations';
import type { ConversationFilter } from './conversations';

const sidebarState: BridgeState = {
  schemaVersion: 1,
  connection: { phase: 'connected', statusText: 'Connected', retryable: false },
  identity: { displayName: 'Alice', userCode: 'A001', admin: false },
  navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
  rooms: [
    { roomName: 'lobby', memberCount: 2, unreadCount: 3, mls: true },
    { roomName: 'study', memberCount: 4, unreadCount: 0 }
  ],
  directMessages: [{ displayName: 'Bob', userCode: 'B002', unreadCount: 1 }],
  activeMessages: [],
  members: [{ displayName: 'Bob', userCode: 'B002', online: true, admin: false }],
  permissions: { activeRoomCanManage: false },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' }
};

function renderSidebar(state = sidebarState, props: Partial<Parameters<typeof ConversationSidebar>[0]> & { filter?: ConversationFilter } = {}) {
  const bridge = createFakeBridge(state);
  const onFilterChange = props.onFilterChange ?? vi.fn();
  const onCreateRoom = props.onCreateRoom ?? vi.fn();
  render(<TooltipProvider><ConversationSidebar state={state} bridge={bridge} filter={props.filter ?? 'rooms'} {...props} onFilterChange={onFilterChange} onCreateRoom={onCreateRoom} /></TooltipProvider>);
  return { bridge, onFilterChange, onCreateRoom };
}

describe('ConversationSidebar', () => {
  afterEach(cleanup);

  it('keeps the brand, the create action and the room list reachable', () => {
    const { onCreateRoom } = renderSidebar();

    expect(screen.getByRole('img', { name: 'LAN Chat 猫咪标识' })).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: '新建频道' }));
    expect(onCreateRoom).toHaveBeenCalledOnce();
    expect(screen.getByRole('button', { name: 'room-lobby' })).toBeInTheDocument();
  });

  it('switches 全部 / 群聊 / 私聊 through tabs that report their selection', () => {
    const { onFilterChange } = renderSidebar();

    expect(screen.getByRole('tab', { name: '群聊' })).toHaveAttribute('aria-selected', 'true');
    expect(screen.getByRole('tab', { name: '私聊' })).toHaveAttribute('aria-selected', 'false');
    fireEvent.mouseDown(screen.getByRole('tab', { name: '私聊' }));
    expect(onFilterChange).toHaveBeenCalledWith('direct');
  });

  it('keeps rooms and direct messages in separate filtered lists', () => {
    expect(buildEntries(sidebarState, 'rooms', '').map((entry) => entry.key)).toEqual(['room:lobby', 'room:study']);
    expect(buildEntries(sidebarState, 'direct', '').map((entry) => entry.key)).toEqual(['dm:B002']);
    expect(buildEntries(sidebarState, 'all', '').map((entry) => entry.key)).toEqual(['room:lobby', 'room:study', 'dm:B002']);
    expect(buildEntries(sidebarState, 'all', 'stu').map((entry) => entry.key)).toEqual(['room:study']);
  });

  it('marks mls rooms with the e2ee lock badge', () => {
    renderSidebar();

    expect(screen.getByLabelText('lobby 端到端加密')).toBeInTheDocument();
    expect(screen.queryByLabelText('study 端到端加密')).not.toBeInTheDocument();
  });

  it('caps unread badges at 99+ and marks the active conversation', () => {
    expect(formatUnread(99)).toBe('99');
    expect(formatUnread(100)).toBe('99+');
    renderSidebar({ ...sidebarState, rooms: [{ roomName: 'lobby', memberCount: 2, unreadCount: 250 }] });

    const lobby = screen.getByRole('button', { name: 'room-lobby' });
    expect(lobby).toHaveAttribute('aria-current', 'page');
    expect(lobby).toHaveTextContent('99+');
  });

  it('shows the online dot only for direct messages whose person is known to be online', () => {
    renderSidebar(sidebarState, { filter: 'direct' });

    const bob = screen.getByRole('button', { name: 'direct-bob' });
    expect(bob.querySelector('[data-presence="online"]')).not.toBeNull();
  });

  it('dispatches typed selection commands and reports the selection', () => {
    const onSelected = vi.fn();
    const { bridge } = renderSidebar(sidebarState, { filter: 'all', onSelected });

    fireEvent.click(screen.getByRole('button', { name: 'room-study' }));
    fireEvent.click(screen.getByRole('button', { name: 'direct-bob' }));

    expect(bridge.commands.map(({ type, payload }) => ({ type, payload }))).toEqual([
      { type: 'conversation.selectRoom', payload: { room: 'study' } },
      { type: 'conversation.selectDirect', payload: { userCode: 'B002' } }
    ]);
    expect(onSelected).toHaveBeenCalledTimes(2);
  });

  it('hides the search field while there are only a few conversations and shows it for many', () => {
    renderSidebar();
    expect(screen.queryByRole('searchbox', { name: '搜索' })).not.toBeInTheDocument();
    cleanup();

    const many = { ...sidebarState, rooms: Array.from({ length: 8 }, (_, index) => ({ roomName: `room_${index}`, memberCount: 2, unreadCount: 0 })) };
    renderSidebar(many);
    fireEvent.change(screen.getByRole('searchbox', { name: '搜索' }), { target: { value: 'room_3' } });

    expect(screen.getByRole('button', { name: 'room-room_3' })).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'room-room_4' })).not.toBeInTheDocument();
  });

  it('offers creating or joining a channel when there are no conversations', () => {
    const { onCreateRoom } = renderSidebar({ ...sidebarState, rooms: [], directMessages: [] });

    const empty = screen.getByText('还没有频道').parentElement!;
    fireEvent.click(within(empty).getByRole('button', { name: /创建或加入频道/ }));
    expect(onCreateRoom).toHaveBeenCalledOnce();
  });
});
