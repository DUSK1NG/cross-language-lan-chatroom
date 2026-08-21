import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

import { App } from './app/App';
import { createFakeBridge, createWebChannelBridge } from './bridge/chatBridge';
import type { BridgeState, ChatBridgeClient } from './bridge/types';

const initialState: BridgeState = {
  schemaVersion: 1,
  connection: { phase: 'idle', statusText: '未连接', retryable: false },
  identity: { displayName: 'Alice', userCode: 'A001', admin: false },
  navigation: { page: 'mode', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
  rooms: [], directMessages: [], activeMessages: [], members: [],
  permissions: { activeRoomCanManage: false },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' }
};

async function bootstrap() {
  let bridge: ChatBridgeClient | undefined;
  if (window.qt?.webChannelTransport) {
    try {
      bridge = await createWebChannelBridge();
    } catch (error) {
      console.error('Unable to connect to ChatBridge; using the local preview bridge.', error);
    }
  }

  bridge ??= createFakeBridge(initialState);
  createRoot(document.getElementById('root')!).render(
    <StrictMode><App bridge={bridge} /></StrictMode>
  );
}

void bootstrap();
