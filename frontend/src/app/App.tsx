import { useEffect, useState } from 'react';

import { createCommand } from '../bridge/chatBridge';
import type { ChatBridgeClient } from '../bridge/types';
import { useBridgeState } from '../state/useBridgeState';
import { WorkspacePage } from './WorkspacePage';
import '../styles/global.css';

type AppProps = { bridge: ChatBridgeClient };

export function App({ bridge }: AppProps) {
  const state = useBridgeState(bridge);
  const [page, setPage] = useState(state.navigation.page);

  useEffect(() => {
    if (state.connection.phase === 'connected') {
      setPage('workspace');
    }
  }, [state.connection.phase]);

  if (page === 'connect') {
    return <RemoteConnectionPage bridge={bridge} state={state} onBack={() => setPage('mode')} />;
  }

  if (page === 'workspace' && state.connection.phase === 'connected') {
    return <WorkspacePage bridge={bridge} state={state} />;
  }

  return <ModeSelectionPage onRemote={() => setPage('connect')} />;
}

function ModeSelectionPage({ onRemote }: { onRemote: () => void }) {
  return (
    <main className="app-shell mode-shell">
      <section className="mode-panel">
        <p className="eyebrow">LAN CHAT / AURORA GLASS</p>
        <h1>选择聊天方式</h1>
        <p className="lede">安全、稳定的 Go + Qt 局域网聊天</p>
        <div className="mode-grid">
          <button className="mode-card" type="button" aria-label="远程服务器" onClick={onRemote}>
            <span className="mode-icon" aria-hidden="true">↗</span>
            <strong>远程服务器</strong>
            <span>连接已经部署的 Go Server</span>
          </button>
          <button className="mode-card" type="button" disabled>
            <span className="mode-icon" aria-hidden="true">⌂</span>
            <strong>创建本地聊天室</strong>
            <span>当前电脑作为 Host</span>
          </button>
          <button className="mode-card" type="button" disabled>
            <span className="mode-icon" aria-hidden="true">◌</span>
            <strong>加入局域网聊天室</strong>
            <span>作为 Guest 加入房主</span>
          </button>
        </div>
      </section>
    </main>
  );
}

function RemoteConnectionPage({ bridge, state, onBack }: {
  bridge: ChatBridgeClient;
  state: ReturnType<typeof useBridgeState>;
  onBack: () => void;
}) {
  const [serverIp, setServerIp] = useState(state.savedConnection.serverIp);
  const [serverPort, setServerPort] = useState(String(state.savedConnection.serverPort));
  const [username, setUsername] = useState(state.savedConnection.username || 'Alice');
  const [userCode, setUserCode] = useState(state.savedConnection.userCode || 'A001');
  const [password, setPassword] = useState('');
  const [caFile, setCaFile] = useState(state.savedConnection.caFile);

  function connect() {
    bridge.dispatch(createCommand('session.connectRemote', {
      serverIp, serverPort: Number(serverPort), username, userCode, password, caFile, registerAccount: false
    }));
    setPassword('');
  }

  return (
    <main className="app-shell connect-shell">
      <section className="connect-panel">
        <p className="eyebrow">SECURE CONNECTION</p>
        <h1>连接远程服务器</h1>
        <p className="status-line">{state.connection.statusText}</p>
        <div className="form-grid">
          <label htmlFor="server-ip">服务器 IP</label>
          <input id="server-ip" value={serverIp} onChange={(event) => setServerIp(event.target.value)} />
          <label htmlFor="server-port">端口</label>
          <input id="server-port" value={serverPort} onChange={(event) => setServerPort(event.target.value)} />
          <label htmlFor="username">用户名</label>
          <input id="username" value={username} onChange={(event) => setUsername(event.target.value)} />
          <label htmlFor="user-code">用户代码</label>
          <input id="user-code" value={userCode} onChange={(event) => setUserCode(event.target.value)} />
          <label htmlFor="password">密码</label>
          <input id="password" type="password" value={password} onChange={(event) => setPassword(event.target.value)} />
          <label htmlFor="ca-file">CA 文件</label>
          <input id="ca-file" value={caFile} onChange={(event) => setCaFile(event.target.value)} />
        </div>
        <div className="form-actions">
          <button className="secondary-button" type="button" onClick={onBack}>返回</button>
          <button className="primary-button" type="button" onClick={connect}>连接</button>
        </div>
      </section>
    </main>
  );
}
