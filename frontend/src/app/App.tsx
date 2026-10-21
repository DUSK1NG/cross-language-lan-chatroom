import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';

import { createCommand } from '../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient } from '../bridge/types';
import { useBridgeState } from '../state/useBridgeState';
import { WorkspacePage } from './WorkspacePage';
import { SettingsPage } from './SettingsPage';
import { inferPrivateKeyPath } from './hostPaths';
import { useFrameTelemetry } from '../performance/useFrameTelemetry';
import { useAppSettings, useResolvedTheme } from '../state/appSettings';
import { Icon, type IconName } from '../components/Icon';
import catBrandMark from '../assets/lan-chat-cat.png';
import { animatePage, animateStagger, setMotionScale } from '../animation/motion';
import { ReleaseNotesCard } from '../changelog/ReleaseNotesCard';
import { hasSeenRelease, markReleaseSeen } from '../changelog/releaseNotes';
import '../styles/global.css';

type AppProps = { bridge: ChatBridgeClient };
type ConnectionMode = 'remote' | 'guest' | 'tunnel';
const discoveryRefreshIntervalMs = 3000;

export function App({ bridge }: AppProps) {
  const state = useBridgeState(bridge);
  useFrameTelemetry(bridge);
  const settings = useAppSettings();
  useResolvedTheme(settings.theme);
  const [page, setPage] = useState(state.navigation.page);
  const [connectionMode, setConnectionMode] = useState<ConnectionMode>('remote');
  const hostAvailable = state.hostDefaults?.available !== false;
  const hostUnavailableReason = state.hostDefaults?.unavailableReason || '此安装包不包含本地服务端';

  const effectsEnabled = state.performance?.effectsEnabled !== false;
  const [releaseOpen, setReleaseOpen] = useState(() => !hasSeenRelease());
  const closeRelease = useCallback(() => { markReleaseSeen(); setReleaseOpen(false); }, []);

  useEffect(() => { setMotionScale(state.performance?.animationDurationScale); }, [state.performance?.animationDurationScale]);

  useEffect(() => {
    document.documentElement.dataset.effects = effectsEnabled ? 'on' : 'off';
  }, [effectsEnabled]);

  useEffect(() => {
    if (state.connection.phase === 'connected' && page !== 'settings') setPage('workspace');
  }, [state.connection.phase]);

  let content;
  if (page === 'connect') {
    content = <RemoteConnectionPage bridge={bridge} state={state} mode={connectionMode} onBack={() => setPage('mode')} />;
  } else if (page === 'host') {
    content = <LocalHostPage bridge={bridge} state={state} onBack={() => setPage('mode')} />;
  } else if (page === 'settings') {
    content = <SettingsPage bridge={bridge} state={state} onBack={() => setPage('workspace')} onShowReleaseNotes={() => setReleaseOpen(true)} />;
  } else if (page === 'workspace' && (state.connection.phase === 'connected' || state.connection.phase === 'reconnecting')) {
    content = <WorkspacePage bridge={bridge} state={state} onSettings={() => setPage('settings')} />;
  } else {
    content = (
      <ModeSelectionPage
        onRemote={() => { setConnectionMode('remote'); setPage('connect'); }}
        onGuest={() => { setConnectionMode('guest'); setPage('connect'); }}
        onTunnel={() => { setConnectionMode('tunnel'); setPage('connect'); }}
        onLocalHost={() => setPage('host')}
        hostAvailable={hostAvailable}
        hostUnavailableReason={hostUnavailableReason}
      />
    );
  }

  return (
    <>
      {content}
      {releaseOpen && <ReleaseNotesCard onClose={closeRelease} />}
    </>
  );
}

/** Animates every non-workspace page on mount (the workspace has its own surface entrance). */
function usePageEntrance<T extends HTMLElement>() {
  const ref = useRef<T>(null);
  useLayoutEffect(() => animatePage(ref.current), []);
  return ref;
}

function ModeSelectionPage({ onRemote, onGuest, onTunnel, onLocalHost, hostAvailable, hostUnavailableReason }: {
  onRemote: () => void;
  onGuest: () => void;
  onTunnel: () => void;
  onLocalHost: () => void;
  hostAvailable: boolean;
  hostUnavailableReason: string;
}) {
  const cards: { label: string; icon: IconName; title: string; copy: string; onClick: () => void; disabled?: boolean; featured?: boolean }[] = [
    { label: 'local-host-mode', icon: 'home', title: '创建本地聊天室', copy: hostAvailable ? '当前电脑启动 Go Server，作为 Host' : hostUnavailableReason, onClick: onLocalHost, disabled: !hostAvailable, featured: true },
    { label: 'guest-mode', icon: 'wifi', title: '加入局域网聊天室', copy: '作为 Guest 连接另一台电脑上的 Host', onClick: onGuest, featured: true },
    { label: 'remote-mode', icon: 'server', title: '远程服务器', copy: '连接已经部署好的 Go Server', onClick: onRemote },
    { label: 'tunnel-mode', icon: 'tunnel', title: '通过 TCP 隧道加入', copy: '使用外部隧道提供的地址、端口和公开证书', onClick: onTunnel }
  ];
  const shellRef = useRef<HTMLElement>(null);
  useLayoutEffect(() => {
    const stopCards = animateStagger(shellRef.current, '.mode-card', { delay: 0.08 });
    const stopHero = animateStagger(shellRef.current, '.mode-hero > *', { distance: 16 });
    return () => { stopCards(); stopHero(); };
  }, []);
  return (
    <main ref={shellRef} className="app-shell mode-shell">
      <section className="mode-hero" aria-label="LAN Chat 简介">
        <img className="mode-hero__mark" src={catBrandMark} alt="" />
        <p className="mode-hero__name">LAN Chat</p>
        <p className="mode-hero__tagline">只在你信任的网络里，安静地聊天。</p>
        <ul className="mode-hero__points">
          <li><Icon name="shield" size={16} />TLS 传输 · 证书指纹校验</li>
          <li><Icon name="lock" size={16} />MLS 端到端加密频道与附件</li>
          <li><Icon name="users" size={16} />房主审批每一次成员连接</li>
        </ul>
      </section>
      <section className="mode-panel">
        <h1>选择聊天方式</h1>
        <p className="lede">安全、稳定的 Go + Qt 局域网聊天</p>
        <div className="mode-grid">
          {cards.map((card) => (
            <button key={card.label} className={`mode-card${card.featured ? ' mode-card--featured' : ''}`} type="button" aria-label={card.label} onClick={card.onClick} disabled={card.disabled}>
              <span className="mode-icon" aria-hidden="true"><Icon name={card.icon} size={20} /></span>
              <span className="mode-card__text">
                <strong>{card.title}</strong>
                <span>{card.copy}</span>
              </span>
              <span className="mode-card__go" aria-hidden="true"><Icon name="arrowRight" size={16} /></span>
            </button>
          ))}
        </div>
      </section>
    </main>
  );
}

function PageHeader({ title, onBack, busy }: { title: string; onBack: () => void; busy: boolean }) {
  return (
    <header className="page-header">
      <button className="icon-button page-header__back" type="button" aria-label="返回模式选择" title="返回" onClick={onBack} disabled={busy}><Icon name="back" /></button>
      <h1>{title}</h1>
    </header>
  );
}

function ConnectionStatus({ state }: { state: BridgeState }) {
  const message = state.connection.lastError?.message;
  if (!message) return <p className="status-line">{state.connection.statusText}</p>;
  return <p className="status-line status-line--error" role="alert">{message}</p>;
}

function RemoteConnectionPage({ bridge, state, mode, onBack }: {
  bridge: ChatBridgeClient;
  state: BridgeState;
  mode: ConnectionMode;
  onBack: () => void;
}) {
  const guest = mode === 'guest';
  const tunnel = mode === 'tunnel';
  const [serverIp, setServerIp] = useState(state.savedConnection.serverIp || '127.0.0.1');
  const [serverPort, setServerPort] = useState(String(state.savedConnection.serverPort || 8888));
  const [username, setUsername] = useState(() => guest || tunnel ? 'Bob' : (state.savedConnection.username || 'Alice'));
  const [userCode, setUserCode] = useState(() => guest || tunnel ? 'B001' : (state.savedConnection.userCode || 'A001'));
  const [caFile, setCaFile] = useState(state.savedConnection.caFile);
  const [selectedHostId, setSelectedHostId] = useState('');
  const [manualEntry, setManualEntry] = useState(!guest);
  const [fingerprintConfirmed, setFingerprintConfirmed] = useState(false);
  const busy = state.connection.phase === 'connecting' || state.connection.phase === 'reconnecting';
  const validPort = Number.isInteger(Number(serverPort)) && Number(serverPort) >= 1 && Number(serverPort) <= 65535;
  const discovery = state.lanDiscovery ?? { scanning: false, hosts: [] };
  const selectedHost = discovery.hosts.find((host) => host.id === selectedHostId);
  const showManualFields = !guest || manualEntry || !selectedHost;

  useEffect(() => {
    if (!guest) return;
    const discover = () => bridge.dispatch(createCommand('session.discoverLanHosts', {}));
    discover();
    const interval = window.setInterval(discover, discoveryRefreshIntervalMs);
    return () => window.clearInterval(interval);
  }, [bridge, guest]);

  useEffect(() => {
    if (selectedHostId && !discovery.hosts.some((host) => host.id === selectedHostId)) {
      setSelectedHostId('');
    }
  }, [discovery.hosts, selectedHostId]);

  useEffect(() => {
    setFingerprintConfirmed(false);
  }, [selectedHostId]);

  function connect() {
    if (busy || !username.trim() || !userCode.trim()) return;
    if (guest && selectedHost && !manualEntry) {
      if (!selectedHost.known && !fingerprintConfirmed) return;
      bridge.dispatch(createCommand('session.connectDiscoveredHost', {
        hostId: selectedHost.id, username: username.trim(), userCode: userCode.trim()
      }));
      return;
    }
    if (!serverIp.trim() || !validPort) return;
    bridge.dispatch(createCommand('session.connectRemote', {
      serverIp: serverIp.trim(), serverPort: Number(serverPort), username: username.trim(),
      userCode: userCode.trim(), caFile: caFile.trim(),
      ...(tunnel ? { tlsServerName: 'localhost' } : {})
    }));
  }

  function selectHost(hostId: string) {
    setSelectedHostId(hostId);
    setManualEntry(false);
  }

  const pageRef = usePageEntrance<HTMLElement>();
  return (
    <main className="app-shell connect-shell">
      <section ref={pageRef} className="connect-panel">
        <PageHeader title={tunnel ? '通过 TCP 隧道加入' : (guest ? '加入局域网聊天室' : '连接远程服务器')} onBack={onBack} busy={busy} />
        <ConnectionStatus state={state} />
        <p className="connection-help">
          {tunnel ? '填写外部隧道提供的主机和端口，并使用房主的公开证书。隧道令牌和私钥只保留在外部隧道工具中。' : (guest ? '自动发现局域网或同一虚拟局域网内的房主；IPv4 变化后会持续重新发现。手动连接仍可作为备用方式。' : '填写已经启动 Go Server 的电脑 IPv4 和端口。')}
        </p>
        {tunnel && <section className="tunnel-guide" aria-label="tunnel-connection-guide">
          <strong><Icon name="shield" size={15} />安全边界</strong>
          <p>LAN Chat 只建立 TLS 聊天连接，不启动或配置隧道，也不会读取隧道令牌、私钥或配置文件。</p>
        </section>}
        {guest && (
          <section className="lan-discovery" aria-label="lan-host-discovery">
            {discovery.scanning && <div className="lan-scan-bar" aria-hidden="true"><span className="lan-scan-bar__fill" /></div>}
            <div className="lan-discovery__header">
              <div>
                <strong>附近聊天室</strong>
                <small>{discovery.scanning ? '正在搜索局域网和虚拟局域网聊天室…' : '自动发现同一局域网或虚拟局域网内的房主'}</small>
              </div>
              <button className="secondary-button button--with-icon" type="button" aria-label="refresh-lan-hosts"
                onClick={() => bridge.dispatch(createCommand('session.discoverLanHosts', {}))} disabled={busy}>
                <Icon name="refresh" size={15} />刷新
              </button>
            </div>
            {discovery.hosts.length === 0 ? (
              <p className="lan-discovery__empty">{discovery.scanning ? '正在等待房主广播…' : '未找到聊天室。请确认房主已启动，并且双方位于同一局域网或同一虚拟局域网。'}</p>
            ) : (
              <div className="lan-discovery__list">
                {discovery.hosts.map((host) => (
                  <button className={`lan-host-card${selectedHostId === host.id && !manualEntry ? ' lan-host-card--selected' : ''}`}
                    type="button" key={host.id} aria-label={`select-lan-host-${host.hostName}`}
                    aria-pressed={selectedHostId === host.id && !manualEntry} onClick={() => selectHost(host.id)} disabled={busy}>
                    <span className="lan-host-card__icon" aria-hidden="true"><Icon name="server" size={18} /></span>
                    <span className="lan-host-card__copy"><strong>{host.hostName}</strong><small>{host.serverIp}:{host.serverPort}</small></span>
                    <span className={`lan-host-card__trust${host.known ? ' lan-host-card__trust--known' : ''}`}>{host.known ? '已信任' : '首次确认'}</span>
                  </button>
                ))}
              </div>
            )}
            {selectedHost && !manualEntry && (
              <div className="lan-discovery__notice">
                <p>点击“确认并加入”后，将保存并固定校验房主的公开证书。指纹：{selectedHost.fingerprintSha256.slice(0, 16)}…</p>
                {!selectedHost.known && <label className="lan-discovery__confirmation">
                  <input type="checkbox" checked={fingerprintConfirmed} onChange={(event) => setFingerprintConfirmed(event.target.checked)} disabled={busy} />
                  我已通过可信渠道核对房主名称和上述指纹
                </label>}
              </div>
            )}
          </section>
        )}
        {guest && selectedHost && !manualEntry && (
          <button className="manual-entry-button" type="button" onClick={() => setManualEntry(true)} disabled={busy}>
            改用手动连接
          </button>
        )}
        <div className="form-grid">
          {showManualFields && <>
            <label htmlFor="server-ip">{tunnel ? '隧道地址' : '服务器 IP'}</label>
            <input id="server-ip" value={serverIp} onChange={(event) => setServerIp(event.target.value)} />
            <label htmlFor="server-port">端口</label>
            <input id="server-port" inputMode="numeric" value={serverPort} onChange={(event) => setServerPort(event.target.value)} />
          </>}
          <label htmlFor="username">用户名</label>
          <input id="username" value={username} onChange={(event) => setUsername(event.target.value)} />
          <label htmlFor="user-code">用户代码</label>
          <input id="user-code" value={userCode} onChange={(event) => setUserCode(event.target.value)} />
          {showManualFields && <>
            <label htmlFor="ca-file">CA 文件</label>
            <input id="ca-file" value={caFile} onChange={(event) => setCaFile(event.target.value)} placeholder="server-lan.crt 的完整路径" />
          </>}
        </div>
        <div className="form-actions">
          <button className="secondary-button" type="button" onClick={onBack} disabled={busy}>返回</button>
          <button className="primary-button" type="button" aria-label={guest && selectedHost && !manualEntry ? 'one-click-join' : 'connect-session'} onClick={connect}
            disabled={busy || !username.trim() || !userCode.trim() || (guest && selectedHost && !manualEntry && !selectedHost.known && !fingerprintConfirmed) || (showManualFields && (!serverIp.trim() || !validPort))}>
            {busy ? '连接中…' : (guest && selectedHost && !manualEntry ? '确认并加入' : '连接')}
          </button>
        </div>
      </section>
    </main>
  );
}

function LocalHostPage({ bridge, state, onBack }: {
  bridge: ChatBridgeClient;
  state: BridgeState;
  onBack: () => void;
}) {
  const defaults: Partial<NonNullable<BridgeState['hostDefaults']>> = state.hostDefaults ?? {};
  const [username, setUsername] = useState('Alice');
  const [userCode, setUserCode] = useState('A001');
  const [serverExe, setServerExe] = useState(() => String(defaults.serverExe ?? ''));
  const [certFile, setCertFile] = useState(() => String(defaults.certFile ?? ''));
  const [keyFile, setKeyFile] = useState(() => String(defaults.keyFile ?? ''));
  const [dbFile, setDbFile] = useState(() => String(defaults.dbFile ?? ''));
  const [keyFileManuallyEdited, setKeyFileManuallyEdited] = useState(false);
  const busy = state.connection.phase === 'connecting' || state.connection.phase === 'reconnecting';
  const canStart = !busy && [username, userCode, serverExe, certFile, keyFile, dbFile].every((value) => value.trim().length > 0);

  useEffect(() => {
    if (keyFileManuallyEdited) return;
    const inferred = inferPrivateKeyPath(serverExe, certFile);
    if (inferred && inferred !== keyFile) setKeyFile(inferred);
  }, [serverExe, certFile, keyFile, keyFileManuallyEdited]);

  function autoDetectKeyFile() {
    setKeyFileManuallyEdited(false);
    const inferred = inferPrivateKeyPath(serverExe, certFile);
    if (inferred) setKeyFile(inferred);
  }

  function startHost() {
    if (!canStart) return;
    bridge.dispatch(createCommand('session.connectLocalHost', {
      serverExe: serverExe.trim(), certFile: certFile.trim(), keyFile: keyFile.trim(), dbFile: dbFile.trim(),
      username: username.trim(), userCode: userCode.trim()
    }));
  }

  const pageRef = usePageEntrance<HTMLElement>();
  return (
    <main className="app-shell connect-shell">
      <section ref={pageRef} className="connect-panel host-panel">
        <PageHeader title="创建本地聊天室" onBack={onBack} busy={busy} />
        <ConnectionStatus state={state} />
        <p className="connection-help">程序会启动本地 Go Server，再自动连接到本机聊天室。其他电脑可通过 Guest 加入。</p>
        <div className="form-grid">
          <label htmlFor="host-username">用户名</label>
          <input id="host-username" value={username} onChange={(event) => setUsername(event.target.value)} />
          <label htmlFor="host-user-code">用户代码</label>
          <input id="host-user-code" value={userCode} onChange={(event) => setUserCode(event.target.value)} />
          <label htmlFor="server-executable">Go Server</label>
          <input id="server-executable" value={serverExe} onChange={(event) => setServerExe(event.target.value)} />
          <label htmlFor="certificate-file">证书文件</label>
          <input id="certificate-file" value={certFile} onChange={(event) => setCertFile(event.target.value)} />
          <label htmlFor="key-file">私钥文件</label>
          <div className="input-with-action">
            <div className="input-action-row">
              <input id="key-file" value={keyFile} onChange={(event) => { setKeyFile(event.target.value); setKeyFileManuallyEdited(true); }} />
              <button className="secondary-button" type="button" aria-label="auto-detect-private-key" onClick={autoDetectKeyFile}>自动检测</button>
            </div>
            <small className="field-hint">{keyFileManuallyEdited ? '已使用手动私钥路径' : '首次启动时会在此路径自动生成私钥'}</small>
          </div>
          <label htmlFor="database-file">数据库文件</label>
          <input id="database-file" value={dbFile} onChange={(event) => setDbFile(event.target.value)} />
        </div>
        <div className="form-actions">
          <button className="secondary-button" type="button" onClick={onBack} disabled={busy}>返回</button>
          <button className="primary-button" type="button" aria-label="start-local-host" onClick={startHost} disabled={!canStart}>{busy ? '启动中…' : '启动并连接'}</button>
        </div>
      </section>
    </main>
  );
}
