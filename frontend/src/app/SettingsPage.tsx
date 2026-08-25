import { createCommand } from '../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient } from '../bridge/types';
import { saveAppSettings, useAppSettings } from '../state/appSettings';

type SettingsPageProps = { bridge: ChatBridgeClient; state: BridgeState; onBack(): void };

export function SettingsPage({ bridge, state, onBack }: SettingsPageProps) {
  const settings = useAppSettings();
  const update = (key: keyof typeof settings, value: boolean) => saveAppSettings({ ...settings, [key]: value });
  const endpoint = `${state.savedConnection.serverIp || '未设置'}:${state.savedConnection.serverPort || '—'}`;
  const performance = state.performance;
  const graphics = state.graphics;
  const diagnostics = state.diagnostics ?? { enabled: false, directory: '' };
  const performanceMode = performance?.mode ?? 'Automatic';
  const modeLabels: Record<string, string> = {
    Automatic: '自动', High: '高性能', Balanced: '均衡', 'Power Saving': '省电'
  };
  return (
    <main className="app-shell settings-shell" data-testid="settings-scroll-container">
      <section className="settings-panel" aria-label="设置">
        <header className="settings-header">
          <button className="secondary-button settings-back" type="button" aria-label="返回聊天" onClick={onBack}>‹</button>
          <div><p className="eyebrow">界面设置</p><h1>设置</h1></div>
        </header>
        <section className="settings-group">
          <h2>界面</h2>
          <label className="settings-row settings-toggle"><span>深色主题</span><input type="checkbox" aria-label="深色主题" checked={settings.darkTheme} onChange={(event) => update('darkTheme', event.target.checked)} /></label>
          <label className="settings-row settings-toggle"><span>显示发送时间</span><input type="checkbox" aria-label="显示发送时间" checked={settings.showSendTime} onChange={(event) => update('showSendTime', event.target.checked)} /></label>
        </section>
        <section className="settings-group">
          <h2>连接</h2>
          <div className="settings-row"><span>当前身份</span><span className="settings-value">{state.identity.displayName}#{state.identity.userCode}</span></div>
          <div className="settings-row"><span>连接模式</span><span className="settings-value">Go TLS Server</span></div>
          <div className="settings-row"><span>服务器地址</span><span className="settings-value">{endpoint}</span></div>
          <div className="settings-row"><span>连接状态</span><span className="settings-value">{state.connection.statusText}</span></div>
          <p className="settings-note">连接配置由启动页面管理。</p>
        </section>
        <section className="settings-group">
          <h2>连接日志</h2>
          <label className="settings-row settings-toggle">
            <span>记录连接日志</span>
            <input
              type="checkbox"
              aria-label="记录连接日志"
              checked={diagnostics.enabled}
              onChange={(event) => bridge.dispatch(createCommand('settings.setConnectionLogging', { enabled: event.target.checked }))}
            />
          </label>
          <p className="settings-note">仅记录时间、服务器端点、重连次数和 TLS/审批结果；不记录消息内容、证书或私钥。</p>
          {diagnostics.enabled && <div className="settings-row"><span>日志目录</span><span className="settings-value">{diagnostics.directory}</span></div>}
        </section>
        <section className="settings-group">
          <h2>性能 / 图形信息</h2>
          {performance && graphics ? <>
            <label className="settings-row settings-select-row">
              <span>性能等级</span>
              <select aria-label="性能等级" value={performanceMode} onChange={(event) => bridge.dispatch(createCommand('settings.setPerformanceMode', { mode: event.target.value }))}>
                {Object.entries(modeLabels).map(([value, label]) => <option key={value} value={value}>{label}</option>)}
              </select>
            </label>
            <div className="settings-row"><span>当前生效</span><span className="settings-value">{modeLabels[performance.effectiveMode] ?? performance.effectiveMode}</span></div>
            <div className="settings-row"><span>渲染 API</span><span className="settings-value">{graphics.graphicsApi}</span></div>
            <div className="settings-row"><span>加速状态</span><span className="settings-value">{graphics.hardwareAcceleration ? '硬件加速' : graphics.softwareRendering ? '软件渲染' : '未知'}</span></div>
            <div className="settings-row"><span>渲染器 / 厂商</span><span className="settings-value">{graphics.renderer} / {graphics.vendor}</span></div>
            <div className="settings-row"><span>屏幕</span><span className="settings-value">{graphics.resolution}</span></div>
            <div className="settings-row"><span>刷新率 / DPI</span><span className="settings-value">{graphics.refreshRate > 0 ? `${graphics.refreshRate.toFixed(1)} Hz / ${graphics.dpi.toFixed(1)}` : 'Unknown'}</span></div>
            <div className="settings-row"><span>自动策略原因</span><span className="settings-value">{performance.automaticReason}</span></div>
            <div className="settings-row"><span>观测帧率</span><span className="settings-value">{performance.observedFrameCount > 0 ? `${performance.observedFps.toFixed(1)} FPS` : '等待样本'}</span></div>
            <div className="settings-row"><span>P95 / 最大帧耗时</span><span className="settings-value">{performance.observedFrameCount > 0 ? `${performance.observedP95FrameMs.toFixed(2)} / ${performance.observedMaxFrameMs.toFixed(2)} ms` : '--'}</span></div>
          </> : <p className="settings-note">性能信息由 Qt bridge 提供，当前连接尚未发布完整运行状态。</p>}
        </section>
      </section>
    </main>
  );
}
