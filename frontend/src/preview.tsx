// 仅用于像素对照的本地预览入口（不参与生产构建，可整文件删除）。
// 用法：http://127.0.0.1:5199/preview.html?theme=dark&accent=orange
import { createRoot } from 'react-dom/client';

import { App } from './app/App';
import { createFakeBridge } from './bridge/chatBridge';
import type { BridgeState } from './bridge/types';

const params = new URLSearchParams(window.location.search);
const theme = params.get('theme') ?? 'dark';
const accent = params.get('accent') ?? 'orange';
const requested = params.get('screen') ?? 'workspace';
const entry = requested === 'mode' || requested === 'online-mode';
const screen = (entry ? 'mode' : requested) as BridgeState['navigation']['page'];
const edition = requested === 'online-mode' ? 'online' : 'lan';
const after = (name: string, fallback: string) => params.get(name) ?? fallback;

window.localStorage.setItem(
  'lan-chat.settings',
  JSON.stringify({
    showSendTime: true, theme, accent, messageSize: 'medium', bubbleRadius: 'medium',
    newText: after('newText', 'fade'),
    bubbleMotion: after('bubbleMotion', 'fly'),
    scrollMotion: after('scrollMotion', 'smooth'),
  }),
);

const state: BridgeState = {
  schemaVersion: 1,
  connection: { phase: screen === 'workspace' || screen === 'settings' ? 'connected' : 'idle', statusText: '已连接', retryable: false },
  edition,
  identity: { displayName: 'Bob', userCode: 'B001', admin: true },
  navigation: { page: screen, activeConversation: { kind: 'room', id: 'game', title: 'game' } },
  rooms: [
    { roomName: 'game', memberCount: 2, unreadCount: 2, ownerCode: 'A001', canManage: true },
    { roomName: 'lobby', memberCount: 0, unreadCount: 0, ownerCode: 'A001', canManage: true },
    { roomName: 'study', memberCount: 0, unreadCount: 0, ownerCode: 'A001', canManage: true },
  ],
  directMessages: [{ displayName: 'Bob', userCode: 'B001', unreadCount: 0 }],
  activeMessages: [
    { messageId: 'm1', displayName: '系统', userCode: '', time: '12:20', content: 'Alice 加入了 game', selfMessage: false, systemMessage: true },
    { messageId: 'm2', displayName: '系统', userCode: '', time: '12:20', content: 'Bob 加入了 game', selfMessage: false, systemMessage: true },
    { messageId: 'm3', displayName: 'Alice', userCode: 'A001', time: '12:21', content: 'lets go', selfMessage: false, systemMessage: false },
    { messageId: 'm4', displayName: 'Bob', userCode: 'B001', time: '12:21', content: 'lets go', selfMessage: true, systemMessage: false, deliveryState: 'delivered' },
    { messageId: 'm5', displayName: 'Alice', userCode: 'A001', time: '12:22', content: '这场我们打双人路，你带个辅助就行。', selfMessage: false, systemMessage: false },
    { messageId: 'm6', displayName: 'Bob', userCode: 'B001', time: '12:22', content: '好，那我先开房间，你直接进。', selfMessage: true, systemMessage: false, deliveryState: 'delivered' },
  ],
  members: [
    { displayName: 'Alice', userCode: 'A001', online: true, admin: false },
    { displayName: 'Bob', userCode: 'B001', online: true, admin: true },
  ],
  permissions: { activeRoomCanManage: true },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Bob', userCode: 'B001', caFile: '' },
  hostDefaults: { available: true, unavailableReason: '' },
  performance: {
    mode: 'Automatic', effectiveMode: 'High', effectsEnabled: true, animationsEnabled: true, gradientsEnabled: true,
    animationDurationScale: 1, observedFrameCount: 0, observedFps: 60, observedP95FrameMs: 16, observedMaxFrameMs: 20,
    automaticReason: 'preview',
  },
};

const bridge = createFakeBridge(state);
// 仅对照用：?arrive=1 时把 fake bridge 挂到 window，CDP 可 publish() 一条新消息来抓入场动画帧。
if (params.get('arrive')) (window as unknown as { __previewBridge?: unknown }).__previewBridge = bridge;

createRoot(document.getElementById('root')!).render(<App bridge={bridge} />);
