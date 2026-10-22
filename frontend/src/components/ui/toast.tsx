import { Toaster as SonnerToaster, toast } from 'sonner';

/** Operation feedback (spec §10): bottom-right, 3 s, stacked, never alert(). */
export function Toaster() {
  return (
    <SonnerToaster position="bottom-right" duration={3000} visibleToasts={4} gap={8}
      toastOptions={{
        unstyled: true,
        classNames: {
          toast: 'flex w-[340px] items-center gap-3 rounded-md bg-elevated px-4 py-3 text-body text-ink shadow-dialog',
          title: 'text-body text-ink',
          description: 'text-small text-ink-2',
          success: '[&_[data-icon]]:text-online',
          error: '[&_[data-icon]]:text-danger',
          warning: '[&_[data-icon]]:text-warning'
        }
      }} />
  );
}

export { toast };
