import { AlertCircle, CheckCircle2, Loader2 } from 'lucide-react';

import { cn } from '../lib/cn';

export type CommandFeedbackProps = {
  status: 'idle' | 'pending' | 'success' | 'error';
  message: string;
};

/** Inline result of one command, next to the control that started it. */
export function CommandFeedback({ status, message }: CommandFeedbackProps) {
  if (status === 'idle' || !message) return null;

  return (
    <p
      className={cn('command-feedback inline-flex items-center gap-2 text-small', `command-feedback--${status}`,
        status === 'error' ? 'text-danger-text' : status === 'success' ? 'text-online' : 'text-ink-2')}
      role={status === 'error' ? 'alert' : 'status'}
      aria-live="polite"
    >
      {status === 'pending' && <Loader2 size={14} aria-hidden="true" className="command-feedback__spinner animate-[spin_0.8s_linear_infinite]" />}
      {status === 'success' && <CheckCircle2 size={14} aria-hidden="true" />}
      {status === 'error' && <AlertCircle size={14} aria-hidden="true" />}
      <span>{message}</span>
    </p>
  );
}
