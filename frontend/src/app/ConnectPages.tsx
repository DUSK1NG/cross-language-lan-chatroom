import { useEffect, useRef, useState, type InputHTMLAttributes, type ReactNode } from 'react';
import { ArrowLeft, ArrowRight, Cable, House, KeyRound, Loader2, Lock, RefreshCw, Server, ShieldCheck, Users, Wifi } from 'lucide-react';

import { createCommand } from '../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient, LanDiscoveredHost } from '../bridge/types';
import catBrandMark from '../assets/lan-chat-cat.png';
import { Button } from '../components/ui/button';
import { IconButton } from '../components/ui/icon-button';
import { cn } from '../lib/cn';

export type ConnectionMode = 'remote' | 'guest' | 'tunnel';
const discoveryRefreshIntervalMs = 3000;
/** How long a selected host may stay missing from the scan list before the selection is dropped. */
const selectedHostGraceMs = 10_000;

const inputClass = 'h-10 w-full min-w-0 rounded-[8px] border-[0.5px] border-divider bg-input px-3 text-body text-ink outline-none transition-ui transition-colors placeholder:text-ink-3 focus:border-accent';

/** Centred page used by every pre-workspace screen. */
function PageFrame({ children, wide = false }: { children: ReactNode; wide?: boolean }) {
  return (
    <main className="scroll-thin flex h-full w-full justify-center overflow-y-auto px-4 py-8 text-ink">
      <section className={cn('my-auto flex w-full flex-col gap-5 rounded-[14px] border-[0.5px] border-divider bg-sidebar p-6', wide ? 'max-w-[720px]' : 'max-w-[460px]')}>
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
    <main className="scroll-thin flex h-full w-full justify-center overflow-y-auto bg-bg px-6 py-10 text-ink">
      <section aria-label="LAN Chat" className="my-auto flex w-full max-w-[400px] flex-col gap-7">
        <header className="flex items-center gap-3">
          <img src={catBrandMark} alt="" className="size-11 rounded-[12px]" />
          <span className="flex min-w-0 flex-col">
            <span className="text-[20px] font-semibold leading-tight tracking-tight">LAN Chat</span>
            <span className="text-small text-ink-2">本地网络聊天 · 端到端加密</span>
          </span>
        </header>
        <ul className="flex flex-col gap-2 text-small text-ink-2">
          <li className="flex items-center gap-2"><ShieldCheck size={14} aria-hidden="true" className="shrink-0 text-ink-3" />TLS 传输 · 证书指纹校验</li>
          <li className="flex items-center gap-2"><Lock size={14} aria-hidden="true" className="shrink-0 text-ink-3" />MLS 端到端加密频道与附件</li>
          <li className="flex items-center gap-2"><Users size={14} aria-hidden="true" className="shrink-0 text-ink-3" />房主审批每一次成员连接</li>
        </ul>
        <div className="flex flex-col gap-2">
          <h1 className="px-1 text-small font-medium text-ink-3">选择聊天方式</h1>
          <div className="overflow-hidden rounded-[12px] border-[0.5px] border-divider bg-sidebar">
            {(lanOnly ? cards.slice(0, 2) : cards).map((card) => (
              <button key={card.label} type="button" aria-label={card.label} onClick={card.onClick} disabled={card.disabled}
                className="press group flex min-h-[58px] w-full cursor-pointer items-center gap-3 border-t border-divider px-3.5 py-2.5 text-left transition-colors first:border-t-0 hover:bg-hover disabled:cursor-not-allowed disabled:opacity-50">
                <span aria-hidden="true" className="shrink-0 text-ink-3">{card.icon}</span>
                <span className="flex min-w-0 flex-1 flex-col">
                  <strong className="text-body font-medium text-ink">{card.title}</strong>
                  <span className="text-small leading-snug text-ink-3">{card.copy}</span>
                </span>
                <ArrowRight size={15} aria-hidden="true" className="shrink-0 text-ink-3 transition-transform transition-ui group-hover:translate-x-0.5" />
              </button>
            ))}
          </div>
        </div>
      </section>
    </main>
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
  const [serverOpen, setServerOpen] = useState(() => !state.savedConnection.serverIp.trim());
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
    <main className="scroll-thin flex h-full w-full justify-center overflow-y-auto bg-bg px-6 py-10 text-ink">
      <section aria-label={register ? '创建账号' : '登录'} className="my-auto flex w-full max-w-[380px] flex-col gap-6">
      <header className="flex items-center gap-3">
        <img src={catBrandMark} alt="" className="size-10 rounded-[10px]" />
        <h1 className="text-[20px] font-semibold tracking-tight">{register ? '创建账号' : '登录'}</h1>
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
        <div className="rounded-[8px] border-[0.5px] border-divider bg-sidebar">
          <button type="button" aria-expanded={serverOpen} aria-label="更改服务器" disabled={busy}
            onClick={() => setServerOpen((open) => !open)}
            className="press flex w-full cursor-pointer items-center gap-2 rounded-[8px] px-3 py-2.5 text-left">
            <Server size={16} aria-hidden="true" className="shrink-0 text-ink-3" />
            <span className="min-w-0 flex-1 truncate text-small text-ink-2">服务器
              <span className="num ml-1 text-ink">{serverIp.trim() || '未填写'}{serverPort ? `:${serverPort}` : ''}</span>
            </span>
            <span className="shrink-0 text-small text-accent-text">{serverOpen ? '收起' : '更改'}</span>
          </button>
          {serverOpen && <div className="grid grid-cols-[1fr_100px] gap-3 px-3 pb-3">
            <Field id="online-server" label="服务器地址" value={serverIp} autoComplete="off" disabled={busy}
              placeholder="chat.example.com" onChange={(event) => setServerIp(event.target.value)} />
            <Field id="online-port" label="端口" value={serverPort} inputMode="numeric" autoComplete="off" disabled={busy}
              onChange={(event) => setServerPort(event.target.value)} />
          </div>}
          {serverOpen && serverPort && !validPort && (
            <p role="status" className="px-3 pb-3 text-small text-warning">端口需为 1–65535 之间的数字。</p>
          )}
        </div>
        <Field id="online-username" label="用户名" name="username" autoComplete="username" spellCheck={false}
          required value={username} disabled={busy} onChange={(event) => setUsername(event.target.value)} />
        {register && <Field id="online-user-code" label="用户代码" value={userCode} autoComplete="off" required disabled={busy}
          placeholder="3–16 位字母或数字" onChange={(event) => setUserCode(event.target.value)} />}
        <Field id="online-password" label="密码" name="password" type="password" autoComplete={register ? 'new-password' : 'current-password'}
          required value={password} disabled={busy} onChange={(event) => setPassword(event.target.value)} />
        {passwordLength > 0 && (passwordLength < 8 || passwordLength > 72) && (
          <p role="status" className="text-small text-warning">密码需 8–72 个 UTF-8 字节，当前为 {passwordLength} 字节。</p>
        )}
        {register && userCode.trim() !== '' && !/^[A-Za-z0-9]{3,16}$/.test(userCode) && (
          <p role="status" className="text-small text-warning">用户代码需为 3–16 位字母或数字。</p>
        )}
        {register && <p className="text-small text-ink-2">密码为 8–72 个 UTF-8 字节。其他人可通过用户代码发起私信。</p>}
        {error ? <p role="alert" className="text-small text-danger-text">{error}</p> : state.connection.phase !== 'idle' && <ConnectionStatus state={state} />}
        <div className="flex flex-col gap-2 pt-1">
          <Button variant="primary" type="submit" className="w-full" disabled={!valid || busy}>{register ? '注册并登录' : '登录'}</Button>
          <Button variant="ghost" className="w-full" disabled={busy} onClick={() => {
            setError(''); setPassword(''); setRegister(!register);
          }}>{register ? '返回登录' : '注册账号'}</Button>
        </div>
      </form>
      </section>
    </main>
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
  // Each scan republishes the host list; remember the last snapshot of the selected host so a
  // round that starts before the owner broadcasts cannot cancel the selection mid-check.
  const liveHost = discovery.hosts.find((host) => host.id === selectedHostId);
  const lastHost = useRef<LanDiscoveredHost | null>(null);
  if (liveHost) lastHost.current = liveHost;
  const selectedHost = liveHost ?? (selectedHostId ? lastHost.current : null);
  const selectedHostMissing = Boolean(selectedHost) && !liveHost;
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

  // A host that really stopped advertising cannot stay selected, but a single scan round is not
  // proof: only drop it after the grace window, and let a fresh selection redo the fingerprint check.
  const hostIds = discovery.hosts.map((host) => host.id).join(',');
  useEffect(() => {
    if (!selectedHostId || discovery.hosts.some((host) => host.id === selectedHostId)) return undefined;
    const timer = window.setTimeout(() => setSelectedHostId((current) => (current === selectedHostId ? '' : current)), selectedHostGraceMs);
    return () => window.clearTimeout(timer);
  }, [selectedHostId, hostIds, discovery.hosts]);

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
                    className={cn('press flex cursor-pointer items-center gap-3 rounded-md px-3 py-2 text-left', selected ? 'bg-active font-medium text-ink' : 'hover:bg-hover')}>
                    <Server size={20} aria-hidden="true" className={selected ? 'text-ink' : 'text-accent-text'} />
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
              {selectedHostMissing && (
                <p role="status" className="flex items-start gap-2 text-warning"><RefreshCw size={14} aria-hidden="true" className="mt-0.5 shrink-0 animate-[spin_2s_linear_infinite]" /><span>暂时没有收到房主广播，正在重新搜索；核对中的选择会保留片刻。</span></p>
              )}
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
