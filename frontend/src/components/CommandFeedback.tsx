export type CommandFeedbackProps = {
  status: 'idle' | 'pending' | 'success' | 'error';
  message: string;
};

export function CommandFeedback({ status, message }: CommandFeedbackProps) {
  if (status === 'idle' || !message) return null;

  return (
    <p
      className={`command-feedback command-feedback--${status}`}
      role={status === 'error' ? 'alert' : 'status'}
      aria-live="polite"
    >
      {status === 'pending' && <span className="command-feedback__spinner" aria-hidden="true" />}
      <span>{message}</span>
    </p>
  );
}
