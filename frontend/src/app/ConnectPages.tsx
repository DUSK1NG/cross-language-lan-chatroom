import { useEffect, useRef, useState, type InputHTMLAttributes, type ReactNode } from 'react';
import { ArrowLeft, ArrowRight, Cable, House, KeyRound, Loader2, Lock, RefreshCw, Server, ShieldCheck, Users, Wifi } from 'lucide-react';

import { createCommand } from '../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient } from '../bridge/types';
import catBrandMark from '../assets/lan-chat-cat.png';
import { Button } from '../components/ui/button';
import { IconButton } from '../components/ui/icon-button';
import { cn } from '../lib/cn';

export type ConnectionMode = 'remote' | 'guest' | 'tunnel';
const discoveryRefreshIntervalMs = 3000;

const inputClass = 'h-10 w-full min-w-0 rounded-md border border-transparent bg-input px-3 text-body text-ink outline-none transition-ui transition-colors placeholder:text-ink-3 focus:border-accent';

/** Centred page used by every pre-workspace screen. */
function PageFrame({ children, wide = false }: { children: ReactNode; wide?: boolean }) {
  return (
    <main className="scroll-thin flex h-full w-full justify-center overflow-y-auto px-4 py-8 text-ink">
      <section className={cn('surface-glass my-auto flex w-full flex-col gap-4 rounded-[24px] p-6 shadow-dialog', wide ? 'max-w-[760px]' : 'max-w-[520px]')}>
        {children}
      </section>
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

export function ModeSelectionPage({ onRemote, onGuest, onTunnel, onLocalHost, hostAvailable, hostUnavailableReason, lanOnly }: {
  onRemote: () => void;
  onGuest: () => void;
  onTunnel: () => void;
  onLocalHost: () => void;
  hostAvailable: boolean;
  hostUnavailableReason: string;
  lanOnly?: boolean;
}) {
  const cards: { label: string; icon: ReactNode; title: string; copy: string; onClick: () => void; disabled?: boolean }[] = [
    { label: 'local-host-mode', icon: <House size={20} />, title: '创建本地聊天室', copy: hostAvailable ? '在当前电脑启动房主服务' : hostUnavailableReason, onClick: onLocalHost, disabled: !hostAvailable },
    { label: 'guest-mode', icon: <Wifi size={20} />, title: '加入局域网聊天室', copy: '作为 Guest 连接另一台电脑上的 Host', onClick: onGuest },
    { label: 'remote-mode', icon: <Server size={20} />, title: '远程服务器', copy: '连接已部署的服务器', onClick: onRemote },
    { label: 'tunnel-mode', icon: <Cable size={20} />, title: '通过 TCP 隧道加入', copy: '使用外部隧道提供的地址、端口和公开证书', onClick: onTunnel }
  ];
  return (
    <PageFrame wide>
      <div className="grid gap-6 min-[760px]:grid-cols-[1fr_1.2fr]">
        <section aria-label="LAN Chat 简介" className="flex flex-col justify-center gap-5 rounded-card bg-bg/70 p-6">
          <img src={catBrandMark} alt="" className="size-16 rounded-[20px] shadow-float" />
          <div className="flex flex-col gap-1">
            <p className="text-[25px] font-semibold tracking-tight">LAN Chat</p>
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
          </div>
          <div className="flex flex-col gap-2">
            {(lanOnly ? cards.slice(0, 2) : cards).map((card) => (
              <button key={card.label} type="button" aria-label={card.label} onClick={card.onClick} disabled={card.disabled}
                className="press group flex min-h-16 cursor-pointer items-center gap-3 rounded-md border border-divider bg-bg/70 px-4 py-3 text-left hover:border-control hover:bg-hover disabled:cursor-not-allowed disabled:opacity-50">
                <span aria-hidden="true" className="inline-flex size-10 shrink-0 items-center justify-center rounded-full bg-accent-soft text-accent-text">{card.icon}</span>
                <span className="flex min-w-0 flex-1 flex-col">
                  <strong className="text-body font-medium text-ink">{card.title}</strong>
                  <span className="text-small text-ink-2">{card.copy}</span>
                </span>
                <ArrowRight size={16} aria-hidden="true" className="shrink-0 text-ink-3 transition-transform transition-ui group-hover:translate-x-1" />
              </button>
            ))}
          </div>
        </section>
      </div>
    </PageFrame>
  );
}

export function OnlineAccountPage({ bridge, state }: { bridge: ChatBridgeClient; state: BridgeState }) {
  const [register, setRegister] = useState(false);
  const [serverIp, setServerIp] = useState(state.savedConnection.serverIp);
  const [serverPort, setServerPort] = useState(String(state.savedConnection.serverPort));
  const [username, setUsername] = useState(state.savedConnection.username);
  const [userCode, setUserCode] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');
  const request = useRef('');
  const busy = state.connection.phase === 'connecting' || state.connection.phase === 'reconnecting';
  const validPort = /^\d+$/.test(serverPort) && Number(serverPort) >= 1 && Number(serverPort) <= 65535;
  const passwordLength = new TextEncoder().encode(password).length;
  const valid = Boolean(serverIp.trim() && username.trim() && password.trim()) && validPort &&
    passwordLength >= 8 && passwordLength <= 72 && (!register || /^[A-Za-z0-9]{3,16}$/.test(userCode));
  useEffect(() => bridge.subscribeCommandResult((result) => {
    if (result.id !== request.current) return;
    request.current = '';
    if (!result.ok) setError(result.error?.message ?? '无法连接在线服务器。');
  }), [bridge]);

  return (
    <PageFrame>
      <header className="flex items-center gap-3">
        <img src={catBrandMark} alt="" className="size-10 rounded-md" />
        <h1 className="text-title font-semibold">{register ? '注册在线账号' : '登录 LAN Chat 在线版'}</h1>
      </header>
      <form className="flex flex-col gap-4" onSubmit={(event) => {
        event.preventDefault();
        if (!valid || busy || request.current) return;
        const command = createCommand('session.connectOnline', {
          serverIp: serverIp.trim(), serverPort: Number(serverPort), username: username.trim(),
          password, register, ...(register ? { userCode } : {})
        });
        setError('');
        request.current = command.id;
        bridge.dispatch(command);
      }}>
        <div className="grid grid-cols-[1fr_100px] gap-3">
          <Field id="online-server" label="服务器地址" value={serverIp} autoComplete="off" required disabled={busy}
            placeholder="chat.example.com" onChange={(event) => setServerIp(event.target.value)} />
          <Field id="online-port" label="端口" value={serverPort} inputMode="numeric" autoComplete="off" required disabled={busy}
            onChange={(event) => setServerPort(event.target.value)} />
        </div>
        <Field id="online-username" label="用户名" name="username" autoComplete="username" spellCheck={false}
          required value={username} disabled={busy} onChange={(event) => setUsername(event.target.value)} />
        {register && <Field id="online-user-code" label="用户代码" value={userCode} autoComplete="off" required disabled={busy}
          placeholder="3–16 位字母或数字" onChange={(event) => setUserCode(event.target.value)} />}
        <Field id="online-password" label="密码" name="password" type="password" autoComplete={register ? 'new-password' : 'current-password'}
          required value={password} disabled={busy} onChange={(event) => setPassword(event.target.value)} />
        {register && <p className="text-small text-ink-2">密码为 8–72 个 UTF-8 字节。其他人可通过用户代码发起私信。</p>}
        {error ? <p role="alert" className="text-small text-danger-text">{error}</p> : state.connection.phase !== 'idle' && <ConnectionStatus state={state} />}
        <div className="flex justify-end gap-2">
          <Button variant="ghost" disabled={busy} onClick={() => {
            setError(''); setPassword(''); setRegister(!register);
          }}>{register ? '返回登录' : '注册账号'}</Button>
          <Button variant="primary" type="submit" disabled={!valid || busy}>{register ? '注册并登录' : '登录'}</Button>
        </div>
      </form>
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
  const [certificateError, setCertificateError] = useState('');
  const certificateRequest = useRef('');
  const certificateName = caFile.split(/[\\/]/).at(-1);
  const [selectedHostId, setSelectedHostId] = useState('');
  const [manualEntry, setManualEntry] = useState(!guest);
  const [fingerprintConfirmed, setFingerprintConfirmed] = useState(false);
  const busy = state.connection.phase === 'connecting' || state.connection.phase === 'reconnecting';
  const validPort = Number.isInteger(Number(serverPort)) && Number(serverPort) >= 1 && Number(serverPort) <= 65535;
  const discovery = state.lanDiscovery ?? { scanning: false, hosts: [] };
  const selectedHost = discovery.hosts.find((host) => host.id === selectedHostId);
  const showManualFields = !guest || manualEntry || !selectedHost;
  const joiningHost = guest && selectedHost && !manualEntry;

  useEffect(() => bridge.subscribeCommandResult((result) => {
    if (result.id !== certificateRequest.current) return;
    certificateRequest.current = '';
    if (!result.ok) setCertificateError(result.error?.message ?? '无法选择公开证书');
    else if (result.certificateFile) setCaFile(result.certificateFile);
  }), [bridge]);

  function chooseCertificate() {
    if (certificateRequest.current) return;
    setCertificateError('');
    const command = createCommand('session.chooseCertificate', {});
    certificateRequest.current = command.id;
    bridge.dispatch(command);
  }

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
        {tunnel ? '填写外部隧道提供的主机和端口，并使用房主的公开证书。隧道令牌和私钥只保留在外部隧道工具中。' : (guest ? '自动发现局域网或同一虚拟局域网内的房主；IPv4 变化后会持续重新发现。手动连接仍可作为备用方式。' : '填写服务端所在电脑的 IPv4 地址和端口。')}
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
              <p className="flex items-start gap-2"><KeyRound size={16} aria-hidden="true" className="mt-0.5 shrink-0" /><span>房主证书指纹<span className="num block break-all">{selectedHost.fingerprintSha256}</span></span></p>
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
        {showManualFields && <div className="flex flex-wrap items-center gap-2 min-[560px]:col-span-2">
          <Button variant="secondary" onClick={chooseCertificate} disabled={busy}>选择公开证书</Button>
          {certificateName && <>
            <span className="min-w-0 max-w-full truncate text-small text-ink-2" title={certificateName}>{certificateName}</span>
            <Button variant="ghost" size="sm" disabled={busy} onClick={() => setCaFile('')}>移除证书</Button>
          </>}
          {certificateError && <p role="alert" className="w-full text-small text-danger-text">{certificateError}</p>}
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
  const [username, setUsername] = useState('Alice');
  const [userCode, setUserCode] = useState('A001');
  const [password, setPassword] = useState('');
  const [startError, setStartError] = useState('');
  const pendingRequest = useRef('');
  const busy = state.connection.phase === 'connecting' || state.connection.phase === 'reconnecting';

  useEffect(() => bridge.subscribeCommandResult((result) => {
    if (result.id !== pendingRequest.current) return;
    pendingRequest.current = '';
    if (!result.ok) setStartError(result.error?.message ?? '无法启动本地聊天室');
  }), [bridge]);

  function startHost() {
    if (busy || pendingRequest.current) return;
    setStartError('');
    const command = createCommand('session.connectLocalHost', { username: username.trim(), userCode: userCode.trim(), password });
    pendingRequest.current = command.id;
    bridge.dispatch(command);
  }

  return (
    <PageFrame>
      <PageHeader title="创建本地聊天室" onBack={onBack} busy={busy} />
      <form className="flex flex-col gap-4" onSubmit={(event) => { event.preventDefault(); startHost(); }}>
        <div className="grid gap-3 min-[560px]:grid-cols-2">
          <Field id="host-username" name="username" label="用户名" autoComplete="username" required value={username} onChange={(event) => setUsername(event.target.value)} />
          <Field id="host-user-code" name="userCode" label="用户代码" required value={userCode} onChange={(event) => setUserCode(event.target.value)} />
        </div>
        <Field id="host-password" name="password" label="密码" type="password" autoComplete="current-password" required value={password} onChange={(event) => setPassword(event.target.value)} />
        {startError ? <p role="alert" className="text-small text-danger-text">{startError}</p> : (busy || state.connection.lastError) && <ConnectionStatus state={state} />}
        <div className="flex justify-end pt-2">
          <Button type="submit" variant="primary" aria-label="start-local-host" disabled={busy}>
            {busy && <Loader2 size={16} aria-hidden="true" className="animate-[spin_0.8s_linear_infinite]" />}
            {busy ? '启动中…' : '启动并连接'}
          </Button>
        </div>
      </form>
    </PageFrame>
  );
}
