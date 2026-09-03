import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { createFakeBridge } from '../bridge/chatBridge';
import type { BridgeState } from '../bridge/types';
import { ConversationSidebar } from './ConversationSidebar';

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
  activeMessages: [], members: [],
  permissions: { activeRoomCanManage: false },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' }
};

describe('ConversationSidebar', () => {
  afterEach(cleanup);

  it('keeps heading actions, search and the room list reachable', () => {
    const bridge = createFakeBridge(sidebarState);
    const onCreateRoom = vi.fn();

    render(<ConversationSidebar state={sidebarState} bridge={bridge} section="rooms" onCreateRoom={onCreateRoom} />);

    expect(screen.getByRole('img', { name: 'LAN Chat 猫咪标识' })).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: '新建频道' }));
    expect(onCreateRoom).toHaveBeenCalledOnce();
    expect(screen.getByRole('searchbox', { name: '搜索群聊' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'room-lobby' })).toBeInTheDocument();
  });

  it('switches sections through the segmented control with aria-selected', () => {
    const bridge = createFakeBridge(sidebarState);
    const onSectionChange = vi.fn();

    render(<ConversationSidebar state={sidebarState} bridge={bridge} section="rooms" onSectionChange={onSectionChange} onCreateRoom={vi.fn()} />);

    expect(screen.getByRole('tab', { name: '群' })).toHaveAttribute('aria-selected', 'true');
    expect(screen.getByRole('tab', { name: '私' })).toHaveAttribute('aria-selected', 'false');
    fireEvent.click(screen.getByRole('tab', { name: '私' }));
    expect(onSectionChange).toHaveBeenCalledWith('direct');
  });

  it('marks mls rooms with the e2ee lock badge', () => {
    const bridge = createFakeBridge(sidebarState);

    render(<ConversationSidebar state={sidebarState} bridge={bridge} section="rooms" onCreateRoom={vi.fn()} />);

    expect(screen.getByLabelText('lobby 端到端加密')).toBeInTheDocument();
    expect(screen.queryByLabelText('study 端到端加密')).not.toBeInTheDocument();
  });

  it('hides the compact navigation and settings buttons when no handlers are given', () => {
    const bridge = createFakeBridge(sidebarState);

    render(<ConversationSidebar state={sidebarState} bridge={bridge} section="rooms" onCreateRoom={vi.fn()} />);

    expect(screen.queryByRole('button', { name: '打开导航' })).not.toBeInTheDocument();
    expect(screen.queryByRole('button', { name: '设置' })).not.toBeInTheDocument();
  });
});
