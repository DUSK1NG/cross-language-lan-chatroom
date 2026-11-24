import { useId, useRef, useState, type ReactNode } from 'react';
import { ArrowLeft, Bell, Info, Palette, Shield, Sparkles, UserRound } from 'lucide-react';

import { createCommand } from '../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient } from '../bridge/types';
import { accentPresets, saveAppSettings, useAppSettings, type AccentPreset, type SizePreference, type ThemePreference } from '../state/appSettings';
import { currentRelease } from '../changelog/releaseNotes';
import catBrandMark from '../assets/lan-chat-cat.png';
import { Avatar } from '../components/ui/avatar';
import { Button } from '../components/ui/button';
import { SegmentedRadio, Select, Switch } from '../components/ui/controls';
import { IconButton } from '../components/ui/icon-button';
import { cn } from '../lib/cn';
import { desktopUpdateRuntime, type DesktopUpdate } from './runtimeBridge';

type SettingsPageProps = { bridge: ChatBridgeClient; state: BridgeState; onBack(): void; onShowReleaseNotes?(): void };
type Category = 'account' | 'appearance' | 'notifications' | 'privacy' | 'about';

const performanceDiagnosticsEnabled = import.meta.env.VITE_ENABLE_PERFORMANCE_DIAGNOSTICS !== 'false';

const categories: { id: Category; label: string; icon: ReactNode }[] = [
  { id: 'account', label: '账户', icon: <UserRound size={20} aria-hidden="true" /> },
  { id: 'appearance', label: '外观', icon: <Palette size={20} aria-hidden="true" /> },
  { id: 'notifications', label: '通知', icon: <Bell size={20} aria-hidden="true" /> },
  { id: 'privacy', label: '隐私与安全', icon: <Shield size={20} aria-hidden="true" /> },
  { id: 'about', label: '关于', icon: <Info size={20} aria-hidden="true" /> }
];

const themeOptions: { value: ThemePreference; label: string }[] = [
  { value: 'system', label: '跟随系统' }, { value: 'light', label: '浅色' }, { value: 'dark', label: '深色' }
];
const accentLabels: Record<AccentPreset, string> = { blue: '蓝', green: '绿', violet: '紫', orange: '橙', rose: '玫红' };
const messageSizeOptions: { value: SizePreference; label: string }[] = [
  { value: 'small', label: '小' }, { value: 'medium', label: '中' }, { value: 'large', label: '大' }
];
const bubbleRadiusOptions: { value: SizePreference; label: string }[] = [
  { value: 'small', label: '小圆角' }, { value: 'medium', label: '标准' }, { value: 'large', label: '大圆角' }
];
const modeLabels: Record<string, string> = { Automatic: '自动', High: '高性能', Balanced: '均衡', 'Power Saving': '省电' };

/** Settings (spec §10): category list on the left, the chosen category on the right. */
export function SettingsPage({ bridge, state, onBack, onShowReleaseNotes }: SettingsPageProps) {
  const settings = useAppSettings();
  const [category, setCategory] = useState<Category>('account');
  const update = <K extends keyof typeof settings>(key: K, value: (typeof settings)[K]) => saveAppSettings({ ...settings, [key]: value });
  const current = categories.find((item) => item.id === category) ?? categories[0];

  return (
    <main className="flex h-full w-full overflow-hidden text-ink">
      <nav aria-label="设置分类" className="surface-chrome flex w-[240px] shrink-0 flex-col border-r border-divider max-[699px]:w-[72px]">
        <header className="flex h-14 shrink-0 items-center gap-2 px-3">
          <IconButton label="返回聊天" onClick={onBack}><ArrowLeft size={20} aria-hidden="true" /></IconButton>
          <h1 className="truncate text-title font-semibold max-[699px]:visually-hidden">设置</h1>
        </header>
        <ul className="flex flex-col gap-1 px-2">
          {categories.map((item) => (
            <li key={item.id}>
              <button type="button" aria-current={item.id === category ? 'page' : undefined} aria-label={item.label} onClick={() => setCategory(item.id)}
                className={cn('press flex h-11 w-full cursor-pointer items-center gap-4 rounded-md px-3 text-body',
                  item.id === category ? 'bg-active text-on-accent' : 'text-ink hover:bg-hover')}>
                <span className={cn('shrink-0', item.id === category ? 'text-on-accent' : 'text-ink-2')}>{item.icon}</span>
                <span className="truncate max-[699px]:visually-hidden">{item.label}</span>
              </button>
            </li>
          ))}
        </ul>
      </nav>
      <section data-testid="settings-scroll-container" aria-label={current.label} className="scroll-thin min-w-0 flex-1 overflow-y-auto">
          <div key={category} className="view-enter mx-auto flex w-full max-w-[640px] flex-col gap-6 px-6 py-8">
            <h2 className="text-xl font-semibold tracking-tight">{current.label}</h2>
            {category === 'account' && <AccountSection state={state} onSignOut={() => {
              bridge.dispatch(createCommand('session.disconnect', {}));
              onBack();
            }} />}
            {category === 'appearance' && <>
              <Group title="主题">
                <Row label="外观主题">
                  <SegmentedRadio label="外观主题" value={settings.theme} onValueChange={(value) => update('theme', value)} options={themeOptions} />
                </Row>
                <Row label="强调色">
                  <SegmentedRadio label="强调色" value={settings.accent} onValueChange={(value) => update('accent', value)}
                    options={accentPresets.map((value) => ({ value, label: accentLabels[value], swatch: `--swatch-${value}` }))} />
                </Row>
              </Group>
              <Group title="聊天">
                <Row label="消息字号">
                  <SegmentedRadio label="消息字号" value={settings.messageSize} onValueChange={(value) => update('messageSize', value)} options={messageSizeOptions} />
                </Row>
                <Row label="气泡圆角">
                  <SegmentedRadio label="气泡圆角" value={settings.bubbleRadius} onValueChange={(value) => update('bubbleRadius', value)} options={bubbleRadiusOptions} />
                </Row>
                <Row label="显示发送时间">
                  <Switch label="显示发送时间" checked={settings.showSendTime} onCheckedChange={(checked) => update('showSendTime', checked)} />
                </Row>
              </Group>
              {performanceDiagnosticsEnabled && <PerformanceSection bridge={bridge} state={state} />}
            </>}
            {category === 'notifications' && (
              <Group title="桌面通知">
                <p className="px-4 py-4 text-body text-ink-2">桌面通知暂未开放。</p>
              </Group>
            )}
            {category === 'privacy' && <PrivacySection bridge={bridge} state={state} />}
            {category === 'about' && (
              <Group title="LAN Chat">
                <div className="flex items-center gap-4 px-4 py-4">
                  <img src={catBrandMark} alt="" className="size-12 rounded-full" />
                  <span className="flex min-w-0 flex-1 flex-col">
                    <strong className="text-name font-semibold">LAN Chat {currentRelease.version}</strong>
                    <small className="text-small text-ink-2">{currentRelease.codename} · {currentRelease.date}</small>
                  </span>
                  {onShowReleaseNotes && <Button variant="secondary" onClick={onShowReleaseNotes}><Sparkles size={16} aria-hidden="true" />版本说明</Button>}
                </div>
                <DesktopUpdatePanel edition={state.edition} />
              </Group>
            )}
          </div>
      </section>
    </main>
  );
}

function DesktopUpdatePanel({ edition }: { edition: BridgeState['edition'] }) {
  const [api] = useState(desktopUpdateRuntime);
  const [update, setUpdate] = useState<DesktopUpdate | null>(null);
  const [busy, setBusy] = useState<'check' | 'download' | 'install' | undefined>();
  const [error, setError] = useState('');
  const [current, setCurrent] = useState(false);
  const [received, setReceived] = useState(0);
  const [downloaded, setDownloaded] = useState(false);
  const confirm = useRef<HTMLDialogElement>(null);
  const titleId = useId();
  const detailsId = useId();
  if (!api) return null;

  const check = async () => {
    setError('');
    setCurrent(false);
    setBusy('check');
    setUpdate(null);
    setDownloaded(false);
    try {
      const found = await api.check();
      setUpdate(found);
      setCurrent(found === null);
    } catch (failure) {
      setError(String(failure));
    } finally {
      setBusy(undefined);
    }
  };

  const download = async (release: DesktopUpdate) => {
    setError('');
    setReceived(0);
    setBusy('download');
    let unlisten: (() => void) | undefined;
    try {
      unlisten = await api.progress(setReceived);
      await api.download(release.version);
      setDownloaded(true);
    } catch (failure) {
      setError(String(failure));
    } finally {
      unlisten?.();
      setBusy(undefined);
    }
  };

  const install = async (release: DesktopUpdate) => {
    setError('');
    setBusy('install');
    try {
      await api.install(release.version);
    } catch (failure) {
      setError(String(failure));
      setBusy(undefined);
    }
  };

  return <div className="flex flex-col gap-3 border-t border-divider px-4 py-3">
    <div className="flex flex-wrap items-center gap-3">
      <Button variant="outline" disabled={busy !== undefined} onClick={() => void check()}>{busy === 'check' ? '正在检查' : '检查更新'}</Button>
      {current && <span role="status" className="text-small text-ink-2">已是最新版本</span>}
    </div>
    {update && <>
      <p className="text-body font-medium">可更新至 {update.version} · {(update.size / 1048576).toFixed(1)} MiB</p>
      {update.date && <time dateTime={update.date} className="text-small text-ink-2">{update.date.slice(0, 10)}</time>}
      {update.notes && <p className="whitespace-pre-wrap text-small text-ink-2">{update.notes}</p>}
      {busy === 'download' && <progress className="w-full" aria-label="下载安装包进度" value={received} max={update.size} />}
      <div>
        {update.installed ? (
          downloaded ? <Button variant="primary" disabled={busy !== undefined} onClick={() => confirm.current?.showModal()}>安装更新</Button>
            : <Button variant="primary" disabled={busy !== undefined} onClick={() => void download(update)}>下载安装包</Button>
        ) : (
          <Button variant="primary" onClick={() => { void api.openZip(update.portableUrl).catch((failure) => setError(String(failure))); }}>下载新版便携 ZIP</Button>
        )}
      </div>
      <dialog ref={confirm} aria-labelledby={titleId} aria-describedby={detailsId} onCancel={(event) => { if (busy === 'install') event.preventDefault(); }}
        className="m-auto w-[min(420px,calc(100%-2rem))] overscroll-contain rounded-card border border-divider bg-bg p-5 text-ink shadow-xl backdrop:overflow-hidden backdrop:overscroll-contain backdrop:bg-black/60">
        <h3 id={titleId} className="text-title font-semibold">安装 {update.version}</h3>
        <p id={detailsId} className="mt-2 text-body text-ink-2">应用将退出并在安装后重新打开。{edition === 'lan' && '开房中的成员会断开。'}</p>
        {error && <p role="alert" className="mt-2 text-small text-danger-text">{error}</p>}
        <form method="dialog" className="mt-4 flex justify-end gap-2">
          <Button type="submit" variant="ghost" autoFocus disabled={busy === 'install'}>稍后安装</Button>
          <Button variant="primary" disabled={busy === 'install'} onClick={() => void install(update)}>安装并重启</Button>
        </form>
      </dialog>
    </>}
    {error && !downloaded && <p role="alert" className="text-small text-danger-text">{error}</p>}
  </div>;
}

function Group({ title, children }: { title: string; children: ReactNode }) {
  return (
    <section aria-label={title} className="flex flex-col gap-2">
      <h3 className="px-4 text-small font-medium text-accent-text">{title}</h3>
      <div className="surface-glass flex flex-col overflow-hidden rounded-card">{children}</div>
    </section>
  );
}

function Row({ label, children, hint }: { label: string; children?: ReactNode; hint?: string }) {
  return (
    <div className="flex min-h-12 flex-wrap items-center justify-between gap-x-4 gap-y-2 border-b border-divider px-4 py-2 last:border-b-0">
      <span className="flex min-w-0 flex-col">
        <span className="text-body text-ink">{label}</span>
        {hint && <small className="text-small text-ink-2">{hint}</small>}
      </span>
      {children}
    </div>
  );
}

function Value({ children }: { children: ReactNode }) {
  return <span className="num min-w-0 break-all text-right text-body text-ink-2">{children}</span>;
}

function AccountSection({ state, onSignOut }: { state: BridgeState; onSignOut: () => void }) {
  const endpoint = `${state.savedConnection.serverIp || '未设置'}:${state.savedConnection.serverPort || '—'}`;
  return <>
    <div className="flex items-center gap-4 rounded-md bg-sidebar px-4 py-4">
      <Avatar name={state.identity.displayName} seed={state.identity.userCode} size={80} />
      <span className="flex min-w-0 flex-col">
        <strong className="truncate text-title font-semibold">{state.identity.displayName}</strong>
        <small className="num text-body text-ink-2">#{state.identity.userCode}{state.identity.admin ? ' · 管理员' : ''}</small>
      </span>
    </div>
    <Group title="连接">
      <Row label="当前身份"><Value>{state.identity.displayName}#{state.identity.userCode}</Value></Row>
      <Row label="连接模式"><Value>{state.edition === 'online' ? '在线' : '局域网'}</Value></Row>
      <Row label="服务器地址"><Value>{endpoint}</Value></Row>
      <Row label="连接状态"><Value>{state.connection.statusText}</Value></Row>
    </Group>
    {state.edition === 'online'
      ? <div className="flex justify-end"><Button variant="outline" onClick={onSignOut}>退出登录</Button></div>
      : <p className="px-4 text-small text-ink-2">用户名和用户代码由启动页面管理，断开后可重新选择。</p>}
  </>;
}

function PrivacySection({ bridge, state }: { bridge: ChatBridgeClient; state: BridgeState }) {
  const diagnostics = state.diagnostics;
  return <>
    <Group title="连接日志">
      <Row label="记录连接日志" hint="仅记录时间、服务器端点、重连次数和 TLS/审批结果；不记录消息内容、证书或私钥。">
        <Switch label="记录连接日志" checked={diagnostics?.enabled === true}
          onCheckedChange={(enabled) => bridge.dispatch(createCommand('settings.setConnectionLogging', { enabled }))} />
      </Row>
    </Group>
    <Group title="加密">
      {state.edition === 'online' ? <>
        <Row label="传输" hint="TLS，校验服务器证书" />
        <Row label="私信历史" hint="由服务器保存，用于同步历史和投递离线消息" />
      </> : <>
        <Row label="传输" hint="TLS，并固定校验房主证书指纹" />
        <Row label="频道与附件" hint="MLS 端到端加密，服务器只转发密文" />
        <Row label="成员审批" hint="用户名和用户代码不是密码,请通过当面或电话等可信渠道核验" />
      </>}
    </Group>
  </>;
}

function PerformanceSection({ bridge, state }: { bridge: ChatBridgeClient; state: BridgeState }) {
  const performance = state.performance;
  const graphics = state.graphics;
  if (!performance || !graphics) {
    return (
      <Group title="性能 / 图形信息">
        <p className="px-4 py-4 text-body text-ink-2">当前没有性能数据。</p>
      </Group>
    );
  }
  const sampled = performance.observedFrameCount > 0;
  return (
    <Group title="性能 / 图形信息">
      <Row label="性能等级">
        <Select label="性能等级" value={performance.mode ?? 'Automatic'} options={Object.entries(modeLabels).map(([value, label]) => ({ value, label }))}
          onValueChange={(mode) => bridge.dispatch(createCommand('settings.setPerformanceMode', { mode }))} />
      </Row>
      <Row label="当前生效"><Value>{modeLabels[performance.effectiveMode] ?? performance.effectiveMode}</Value></Row>
      <Row label="渲染 API"><Value>{graphics.graphicsApi}</Value></Row>
      <Row label="加速状态"><Value>{graphics.hardwareAcceleration ? '硬件加速' : graphics.softwareRendering ? '软件渲染' : '未知'}</Value></Row>
      <Row label="渲染器 / 厂商"><Value>{graphics.renderer} / {graphics.vendor}</Value></Row>
      <Row label="屏幕"><Value>{graphics.resolution}</Value></Row>
      <Row label="刷新率 / DPI"><Value>{graphics.refreshRate > 0 ? `${graphics.refreshRate.toFixed(1)} Hz / ${graphics.dpi.toFixed(1)}` : 'Unknown'}</Value></Row>
      <Row label="自动策略原因"><Value>{performance.automaticReason}</Value></Row>
      <Row label="观测帧率"><Value>{sampled ? `${performance.observedFps.toFixed(1)} FPS` : '等待样本'}</Value></Row>
      <Row label="P95 / 最大帧耗时"><Value>{sampled ? `${performance.observedP95FrameMs.toFixed(2)} / ${performance.observedMaxFrameMs.toFixed(2)} ms` : '--'}</Value></Row>
    </Group>
  );
}
