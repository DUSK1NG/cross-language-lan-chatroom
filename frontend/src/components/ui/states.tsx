import type { ReactNode } from 'react';
import { AlertTriangle } from 'lucide-react';

import { cn } from '../../lib/cn';
import { Button } from './button';

/** Skeleton block: a flat pulse at the content's real size — never a spinner (spec §10). */
export function Skeleton({ className }: { className?: string }) {
  return <span aria-hidden="true" className={cn('block rounded-sm bg-hover [animation:skeleton-pulse_1.4s_var(--ease)_infinite]', className)} />;
}

/** Empty state: icon + one sentence + the primary action. */
export function EmptyState({ icon, title, description, action, className }: { icon: ReactNode; title: string; description?: string; action?: ReactNode; className?: string }) {
  return (
    <div className={cn('flex flex-col items-center justify-center gap-2 px-6 py-8 text-center', className)}>
      <span aria-hidden="true" className="mb-2 text-ink-3 [&_svg]:size-12">{icon}</span>
      <p className="text-title font-semibold text-ink">{title}</p>
      {description && <p className="max-w-[280px] text-small text-ink-2">{description}</p>}
      {action && <div className="mt-3">{action}</div>}
    </div>
  );
}

/** Error state: the reason + a retry action, inside the block that failed. */
export function ErrorState({ reason, onRetry, className }: { reason: string; onRetry?: () => void; className?: string }) {
  return (
    <div role="alert" className={cn('flex flex-col items-center justify-center gap-3 px-6 py-8 text-center', className)}>
      <AlertTriangle aria-hidden="true" className="size-8 text-warning" />
      <p className="max-w-[280px] text-small text-ink-2">{reason}</p>
      {onRetry && <Button variant="outline" size="sm" onClick={onRetry}>重试</Button>}
    </div>
  );
}
