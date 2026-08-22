import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

import { App } from './app/App';
import { BridgeUnavailablePage } from './app/BridgeUnavailablePage';
import { resolveRuntimeBridge } from './app/runtimeBridge';
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
  const resolution = await resolveRuntimeBridge({
    hasTransport: Boolean(window.qt?.webChannelTransport),
    isDevelopment: import.meta.env.DEV,
    createBridge: createWebChannelBridge,
    createPreviewBridge: () => createFakeBridge(initialState)
  });
  const root = createRoot(document.getElementById('root')!);
  if (resolution.error || !resolution.bridge) {
    root.render(<BridgeUnavailablePage message={resolution.error ?? 'Qt bridge 未连接'} onRetry={() => window.location.reload()} />);
    return;
  }
  root.render(<StrictMode><App bridge={resolution.bridge} /></StrictMode>);
}

void bootstrap();
