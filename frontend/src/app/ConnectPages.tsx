import { useEffect, useState, type InputHTMLAttributes, type ReactNode } from 'react';
import { motion } from 'framer-motion';
import { ArrowLeft, ArrowRight, Cable, House, KeyRound, Loader2, Lock, RefreshCw, Server, ShieldCheck, Users, Wifi } from 'lucide-react';

import { createCommand } from '../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient } from '../bridge/types';
import catBrandMark from '../assets/lan-chat-cat.png';
import { Button } from '../components/ui/button';
import { IconButton } from '../components/ui/icon-button';
import { cn } from '../lib/cn';
import { ease } from '../lib/motion';
import { inferPrivateKeyPath } from './hostPaths';

export type ConnectionMode = 'remote' | 'guest' | 'tunnel';
const discoveryRefreshIntervalMs = 3000;

const inputClass = 'h-10 w-full min-w-0 rounded-md border border-transparent bg-input px-3 text-body text-ink outline-none transition-ui transition-colors placeholder:text-ink-3 focus:border-accent';

/** Centred page used by every pre-workspace screen. */
function PageFrame({ children, wide = false }: { children: ReactNode; wide?: boolean }) {
  return (
    <main className="scroll-thin flex h-full w-full justify-center overflow-y-auto bg-bg px-4 py-8 text-ink">
      <motion.section initial={{ opacity: 0, y: 8 }} animate={{ opacity: 1, y: 0 }} transition={{ duration: 0.3, ease }}
        className={cn('my-auto flex w-full flex-col gap-4 rounded-card bg-sidebar p-6 shadow-float', wide ? 'max-w-[760px]' : 'max-w-[520px]')}>
        {children}
      </motion.section>
    </main>
  );
}

function Field({ id, label, ...props }: { id: string; label: string } & InputHTMLAttributes<HTMLInputElement>) {
  return (
    <div className="flex flex-col gap-1">
      <label htmlFor={id} className="text-small font-medium text-ink-2">{label}</label>
      <input id={id} className={inputClass} {...props} />
    </div>
  );
}

function PageHeader({ title, onBack, busy }: { title: string; onBack: () => void; busy: boolean }) {
  return (
    <header className="-ml-2 flex items-center gap-2">
      <IconButton label="返回模式选择" tooltip="返回" onClick={onBack} disabled={busy}><ArrowLeft size={20} aria-hidden="true" /></IconButton>
      <h1 className="text-title font-semibold">{title}</h1>
    </header>
  );
}

function ConnectionStatus({ state }: { state: BridgeState }) {
  const message = state.connection.lastError?.message;
  if (!message) return <p className="text-small text-ink-2">{state.connection.statusText}</p>;
  return <p role="alert" className="rounded-md bg-hover px-3 py-2 text-small text-danger-text">{message}</p>;
}

export function ModeSelectionPage({ onRemote, onGuest, onTunnel, onLocalHost, hostAvailable, hostUnavailableReason }: {
  onRemote: () => void;
  onGuest: () => void;
  onTunnel: () => void;
  onLocalHost: () => void;
  hostAvailable: boolean;
  hostUnavailableReason: string;
}) {
  const cards: { label: string; icon: ReactNode; title: string; copy: string; onClick: () => void; disabled?: boolean }[] = [
    { label: 'local-host-mode', icon: <House size={20} />, title: '创建本地聊天室', copy: hostAvailable ? '当前电脑启动 Go Server，作为 Host' : hostUnavailableReason, onClick: onLocalHost, disabled: !hostAvailable },
    { label: 'guest-mode', icon: <Wifi size={20} />, title: '加入局域网聊天室', copy: '作为 Guest 连接另一台电脑上的 Host', onClick: onGuest },
    { label: 'remote-mode', icon: <Server size={20} />, title: '远程服务器', copy: '连接已经部署好的 Go Server', onClick: onRemote },
    { label: 'tunnel-mode', icon: <Cable size={20} />, title: '通过 TCP 隧道加入', copy: '使用外部隧道提供的地址、端口和公开证书', onClick: onTunnel }
  ];
  return (
    <PageFrame wide>
      <div className="grid gap-6 min-[760px]:grid-cols-[1fr_1.2fr]">
        <section aria-label="LAN Chat 简介" className="flex flex-col gap-4 rounded-md bg-bg p-6">
          <img src={catBrandMark} alt="" className="size-16 rounded-full" />
          <div className="flex flex-col gap-1">
            <p className="text-title font-semibold">LAN Chat</p>
            <p className="text-body text-ink-2">只在你信任的网络里，安静地聊天。</p>
          </div>
          <ul className="flex flex-col gap-3 text-small text-ink-2">
            <li className="flex items-center gap-2"><ShieldCheck size={16} aria-hidden="true" className="text-accent-text" />TLS 传输 · 证书指纹校验</li>
            <li className="flex items-center gap-2"><Lock size={16} aria-hidden="true" className="text-accent-text" />MLS 端到端加密频道与附件</li>
            <li className="flex items-center gap-2"><Users size={16} aria-hidden="true" className="text-accent-text" />房主审批每一次成员连接</li>
          </ul>
        </section>
        <section className="flex flex-col gap-4">
          <div className="flex flex-col gap-1">
            <h1 className="text-title font-semibold">选择聊天方式</h1>
            <p className="text-small text-ink-2">安全、稳定的 Go + Qt 局域网聊天</p>
          </div>
          <div className="flex flex-col gap-2">
            {cards.map((card, index) => (
              <motion.button key={card.label} type="button" aria-label={card.label} onClick={card.onClick} disabled={card.disabled}
                initial={{ opacity: 0, y: 8 }} animate={{ opacity: 1, y: 0 }} transition={{ duration: 0.2, ease, delay: 0.04 * index }}
                className="press group flex min-h-16 cursor-pointer items-center gap-3 rounded-md bg-bg px-4 py-3 text-left hover:bg-hover disabled:cursor-not-allowed disabled:opacity-50">
                <span aria-hidden="true" className="inline-flex size-10 shrink-0 items-center justify-center rounded-full bg-accent-soft text-accent-text">{card.icon}</span>
                <span className="flex min-w-0 flex-1 flex-col">
                  <strong className="text-body font-medium text-ink">{card.title}</strong>
                  <span className="text-small text-ink-2">{card.copy}</span>
                </span>
                <ArrowRight size={16} aria-hidden="true" className="shrink-0 text-ink-3 transition-transform transition-ui group-hover:translate-x-1" />
              </motion.button>
            ))}
          </div>
        </section>
      </div>
    </PageFrame>
  );
}

export function RemoteConnectionPage({ bridge, state, mode, onBack }: {
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
  const joiningHost = guest && selectedHost && !manualEntry;

  useEffect(() => {
    if (!guest) return;
    const discover = () => bridge.dispatch(createCommand('session.discoverLanHosts', {}));
    discover();
    const interval = window.setInterval(discover, discoveryRefreshIntervalMs);
    return () => window.clearInterval(interval);
  }, [bridge, guest]);

  // A host that stopped advertising cannot stay selected; a new selection needs a fresh fingerprint check.
  const hostIds = discovery.hosts.map((host) => host.id).join(',');
  const [trackedHostIds, setTrackedHostIds] = useState(hostIds);
  if (trackedHostIds !== hostIds) {
    setTrackedHostIds(hostIds);
    if (selectedHostId && !discovery.hosts.some((host) => host.id === selectedHostId)) setSelectedHostId('');
  }

  function connect() {
    if (busy || !username.trim() || !userCode.trim()) return;
    if (joiningHost) {
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
    setFingerprintConfirmed(false);
  }

  const disabled = busy || !username.trim() || !userCode.trim()
    || Boolean(joiningHost && !selectedHost.known && !fingerprintConfirmed)
    || (showManualFields && (!serverIp.trim() || !validPort));

  return (
    <PageFrame>
      <PageHeader title={tunnel ? '通过 TCP 隧道加入' : (guest ? '加入局域网聊天室' : '连接远程服务器')} onBack={onBack} busy={busy} />
      <ConnectionStatus state={state} />
      <p className="text-body text-ink-2">
        {tunnel ? '填写外部隧道提供的主机和端口，并使用房主的公开证书。隧道令牌和私钥只保留在外部隧道工具中。' : (guest ? '自动发现局域网或同一虚拟局域网内的房主；IPv4 变化后会持续重新发现。手动连接仍可作为备用方式。' : '填写已经启动 Go Server 的电脑 IPv4 和端口。')}
      </p>
      {tunnel && <section aria-label="tunnel-connection-guide" className="flex flex-col gap-1 rounded-md bg-bg px-4 py-3">
        <strong className="flex items-center gap-2 text-body font-medium"><ShieldCheck size={16} aria-hidden="true" className="text-accent-text" />安全边界</strong>
        <p className="text-small text-ink-2">LAN Chat 只建立 TLS 聊天连接，不启动或配置隧道，也不会读取隧道令牌、私钥或配置文件。</p>
      </section>}
      {guest && (
        <section aria-label="lan-host-discovery" className="relative flex flex-col gap-3 overflow-hidden rounded-md bg-bg p-4">
          {discovery.scanning && <span aria-hidden="true" className="absolute inset-x-0 top-0 h-0.5 animate-[skeleton-pulse_1.2s_ease-in-out_infinite] bg-accent" />}
          <div className="flex items-center gap-3">
            <div className="flex min-w-0 flex-1 flex-col">
              <strong className="text-body font-medium">附近聊天室</strong>
              <small className="text-small text-ink-2">{discovery.scanning ? '正在搜索局域网和虚拟局域网聊天室…' : '自动发现同一局域网或虚拟局域网内的房主'}</small>
            </div>
            <Button variant="secondary" size="sm" aria-label="refresh-lan-hosts" disabled={busy}
              onClick={() => bridge.dispatch(createCommand('session.discoverLanHosts', {}))}>
              <RefreshCw size={16} aria-hidden="true" />刷新
            </Button>
          </div>
          {discovery.hosts.length === 0 ? (
            <p className="text-small text-ink-2">{discovery.scanning ? '正在等待房主广播…' : '未找到聊天室。请确认房主已启动，并且双方位于同一局域网或同一虚拟局域网。'}</p>
          ) : (
            <div className="flex flex-col gap-1">
              {discovery.hosts.map((host) => {
                const selected = selectedHostId === host.id && !manualEntry;
                return (
                  <button key={host.id} type="button" aria-label={`select-lan-host-${host.hostName}`} aria-pressed={selected}
                    onClick={() => selectHost(host.id)} disabled={busy}
                    className={cn('press flex cursor-pointer items-center gap-3 rounded-md px-3 py-2 text-left', selected ? 'bg-active text-on-accent' : 'hover:bg-hover')}>
                    <Server size={20} aria-hidden="true" className={selected ? 'text-on-accent' : 'text-accent-text'} />
                    <span className="flex min-w-0 flex-1 flex-col">
                      <strong className="truncate text-body font-medium">{host.hostName}</strong>
                      <small className={cn('num text-small', selected ? 'text-on-active-2' : 'text-ink-2')}>{host.serverIp}:{host.serverPort}</small>
                    </span>
                    <span className={cn('shrink-0 rounded-sm px-2 text-small', host.known ? 'bg-accent-soft text-accent-text' : 'bg-hover text-warning')}>{host.known ? '已信任' : '首次确认'}</span>
                  </button>
                );
              })}
            </div>
          )}
          {joiningHost && (
            <div className="flex flex-col gap-2 border-t border-divider pt-3 text-small text-ink-2">
              <p className="flex gap-2"><KeyRound size={16} aria-hidden="true" className="mt-0.5 shrink-0" />点击“确认并加入”后，将保存并固定校验房主的公开证书。指纹：<span className="num break-all">{selectedHost.fingerprintSha256.slice(0, 16)}…</span></p>
              {!selectedHost.known && <label className="flex cursor-pointer items-start gap-2 text-ink">
                <input type="checkbox" className="mt-0.5 size-4 accent-[var(--accent)]" checked={fingerprintConfirmed}
                  onChange={(event) => setFingerprintConfirmed(event.target.checked)} disabled={busy} />
                我已通过可信渠道核对房主名称和上述指纹
              </label>}
            </div>
          )}
        </section>
      )}
      {joiningHost && (
        <button type="button" onClick={() => setManualEntry(true)} disabled={busy}
          className="press self-start cursor-pointer text-small text-accent-text hover:underline">改用手动连接</button>
      )}
      <div className="grid gap-3 min-[560px]:grid-cols-2">
        {showManualFields && <>
          <Field id="server-ip" label={tunnel ? '隧道地址' : '服务器 IP'} value={serverIp} onChange={(event) => setServerIp(event.target.value)} />
          <Field id="server-port" label="端口" inputMode="numeric" value={serverPort} onChange={(event) => setServerPort(event.target.value)} />
        </>}
        <Field id="username" label="用户名" value={username} onChange={(event) => setUsername(event.target.value)} />
        <Field id="user-code" label="用户代码" value={userCode} onChange={(event) => setUserCode(event.target.value)} />
        {showManualFields && <div className="min-[560px]:col-span-2">
          <Field id="ca-file" label="CA 文件" value={caFile} onChange={(event) => setCaFile(event.target.value)} placeholder="server-lan.crt 的完整路径" />
        </div>}
      </div>
      <div className="flex justify-end gap-2 pt-2">
        <Button variant="ghost" onClick={onBack} disabled={busy}>返回</Button>
        <Button variant="primary" aria-label={joiningHost ? 'one-click-join' : 'connect-session'} onClick={connect} disabled={disabled}>
          {busy && <Loader2 size={16} aria-hidden="true" className="animate-[spin_0.8s_linear_infinite]" />}
          {busy ? '连接中…' : (joiningHost ? '确认并加入' : '连接')}
        </Button>
      </div>
    </PageFrame>
  );
}

export function LocalHostPage({ bridge, state, onBack }: {
  bridge: ChatBridgeClient;
  state: BridgeState;
  onBack: () => void;
}) {
  const defaults: Partial<NonNullable<BridgeState['hostDefaults']>> = state.hostDefaults ?? {};
  const [username, setUsername] = useState('Alice');
  const [userCode, setUserCode] = useState('A001');
  const [serverExe, setServerExe] = useState(() => String(defaults.serverExe ?? ''));
  const [certFile, setCertFile] = useState(() => String(defaults.certFile ?? ''));
  const [keyFileInput, setKeyFile] = useState(() => String(defaults.keyFile ?? ''));
  const [dbFile, setDbFile] = useState(() => String(defaults.dbFile ?? ''));
  const [keyFileManuallyEdited, setKeyFileManuallyEdited] = useState(false);
  // Until the key path is edited by hand it follows the certificate (same folder, .key).
  const keyFile = keyFileManuallyEdited ? keyFileInput : (inferPrivateKeyPath(serverExe, certFile) || keyFileInput);
  const busy = state.connection.phase === 'connecting' || state.connection.phase === 'reconnecting';
  const canStart = !busy && [username, userCode, serverExe, certFile, keyFile, dbFile].every((value) => value.trim().length > 0);

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

  return (
    <PageFrame>
      <PageHeader title="创建本地聊天室" onBack={onBack} busy={busy} />
      <ConnectionStatus state={state} />
      <p className="text-body text-ink-2">程序会启动本地 Go Server，再自动连接到本机聊天室。其他电脑可通过 Guest 加入。</p>
      <div className="grid gap-3 min-[560px]:grid-cols-2">
        <Field id="host-username" label="用户名" value={username} onChange={(event) => setUsername(event.target.value)} />
        <Field id="host-user-code" label="用户代码" value={userCode} onChange={(event) => setUserCode(event.target.value)} />
      </div>
      <Field id="server-executable" label="Go Server" value={serverExe} onChange={(event) => setServerExe(event.target.value)} />
      <Field id="certificate-file" label="证书文件" value={certFile} onChange={(event) => setCertFile(event.target.value)} />
      <div className="flex flex-col gap-1">
        <label htmlFor="key-file" className="text-small font-medium text-ink-2">私钥文件</label>
        <div className="flex gap-2">
          <input id="key-file" className={inputClass} value={keyFile} onChange={(event) => { setKeyFile(event.target.value); setKeyFileManuallyEdited(true); }} />
          <Button variant="secondary" className="h-10" aria-label="auto-detect-private-key" onClick={autoDetectKeyFile}>自动检测</Button>
        </div>
        <small className="text-small text-ink-3">{keyFileManuallyEdited ? '已使用手动私钥路径' : '首次启动时会在此路径自动生成私钥'}</small>
      </div>
      <Field id="database-file" label="数据库文件" value={dbFile} onChange={(event) => setDbFile(event.target.value)} />
      <div className="flex justify-end gap-2 pt-2">
        <Button variant="ghost" onClick={onBack} disabled={busy}>返回</Button>
        <Button variant="primary" aria-label="start-local-host" onClick={startHost} disabled={!canStart}>
          {busy && <Loader2 size={16} aria-hidden="true" className="animate-[spin_0.8s_linear_infinite]" />}
          {busy ? '启动中…' : '启动并连接'}
        </Button>
      </div>
    </PageFrame>
  );
}
