type BridgeUnavailablePageProps = { message: string; onRetry(): void };

export function BridgeUnavailablePage({ message, onRetry }: BridgeUnavailablePageProps) {
  return (
    <main className="app-shell mode-shell bridge-error-shell">
      <section className="mode-panel bridge-error-panel">
        <p className="eyebrow">LAN CHAT / CONNECTION ERROR</p>
        <h1>界面暂时不可用</h1>
        <p className="status-line status-line--error" role="alert">{message}</p>
        <p className="lede">Qt bridge 未连接，当前不会伪造聊天状态或吞掉操作。</p>
        <button className="primary-button" type="button" onClick={onRetry}>重试连接</button>
      </section>
    </main>
  );
}
