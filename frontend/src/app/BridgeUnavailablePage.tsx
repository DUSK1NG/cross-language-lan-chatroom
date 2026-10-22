import type { ReactNode } from 'react';
import { AlertTriangle, Unplug } from 'lucide-react';

import { Button } from '../components/ui/button';

/** Full-window error screen shared by the bridge-unavailable page and the error boundary. */
export function StatusScreen({ icon, title, alert, note, actions }: { icon: ReactNode; title: string; alert: string; note: string; actions: ReactNode }) {
  return (
    <main className="flex h-full w-full items-center justify-center bg-bg p-4 text-ink">
      <section className="flex w-full max-w-[440px] flex-col items-center gap-4 rounded-card bg-sidebar p-8 text-center shadow-float">
        <span aria-hidden="true" className="inline-flex size-14 items-center justify-center rounded-full bg-hover text-danger-text">{icon}</span>
        <h1 className="text-title font-semibold">{title}</h1>
        <p role="alert" className="text-body text-danger-text">{alert}</p>
        <p className="text-small text-ink-2">{note}</p>
        <div className="flex gap-2 pt-2">{actions}</div>
      </section>
    </main>
  );
}

type BridgeUnavailablePageProps = { message: string; onRetry(): void };

export function BridgeUnavailablePage({ message, onRetry }: BridgeUnavailablePageProps) {
  return (
    <StatusScreen icon={<Unplug size={28} />} title="界面暂时不可用" alert={message}
      note="Qt bridge 未连接，当前不会伪造聊天状态或吞掉操作。"
      actions={<Button variant="primary" onClick={onRetry}>重试连接</Button>} />
  );
}

export const errorIcon = <AlertTriangle size={28} />;
